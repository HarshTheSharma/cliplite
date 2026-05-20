"""Train a tiny kill feed classifier from collected frames.

Setup:
    pip install torch torchvision

Run after collect.py has gathered enough data (300+ pos, 600+ neg):
    python train.py

Outputs:
    kill_net.pt          — PyTorch checkpoint
    kill_net_weights.h   — C++ header with raw weights for zero-dependency inference
"""

import io
import random
import struct

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset, random_split
from torchvision import transforms
from PIL import Image, ImageFilter
from pathlib import Path

# ── Quality augmentations ─────────────────────────────────────────────────────

class RandomJpeg:
    """Simulate JPEG compression artifacts (screenshots, clip encoding)."""
    def __init__(self, quality_low=40, quality_high=95):
        self.low  = quality_low
        self.high = quality_high

    def __call__(self, img: Image.Image) -> Image.Image:
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=random.randint(self.low, self.high))
        buf.seek(0)
        return Image.open(buf).copy()


class RandomBlur:
    """Occasional extra blur — simulates motion blur or low AA settings."""
    def __init__(self, sigma_max=1.5, p=0.25):
        self.sigma_max = sigma_max
        self.p         = p

    def __call__(self, img: Image.Image) -> Image.Image:
        if random.random() < self.p:
            sigma = random.uniform(0.3, self.sigma_max)
            img   = img.filter(ImageFilter.GaussianBlur(radius=sigma))
        return img


class RandomNoise:
    """Additive Gaussian noise on a tensor — simulates sensor noise."""
    def __init__(self, std_max=0.04, p=0.5):
        self.std_max = std_max
        self.p       = p

    def __call__(self, t: torch.Tensor) -> torch.Tensor:
        if random.random() < self.p:
            t = t + torch.randn_like(t) * random.uniform(0.0, self.std_max)
        return t


# ── Dataset ───────────────────────────────────────────────────────────────────

class KillFeedDataset(Dataset):
    def __init__(self, root: Path, transform=None):
        pos = [(p, 1) for p in (root / "positive").glob("*.png")]
        neg = [(p, 0) for p in (root / "negative").glob("*.png")]
        self.samples = pos + neg
        random.shuffle(self.samples)
        self.transform = transform
        print(f"Dataset: {len(pos)} positive, {len(neg)} negative")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, i):
        path, label = self.samples[i]
        img = Image.open(path).convert("RGB")
        if self.transform:
            img = self.transform(img)
        return img, label


# ── Model ─────────────────────────────────────────────────────────────────────
# Input: 3 × 96 × 128  (RGB, three 32px strips stacked — color patterns only)
# Three conv blocks, two FC layers, binary output.
# After 3× MaxPool2d(2): 96→12 height, 128→16 width → 64×12×16 = 12288 features.
# Total parameters: ~1.7M — runs in <2ms on CPU.

class KillNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.features = nn.Sequential(
            # Block 1: 3×48×128 → 16×24×64
            nn.Conv2d(3, 16, kernel_size=3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),

            # Block 2: 16×24×64 → 32×12×32
            nn.Conv2d(16, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),

            # Block 3: 32×12×32 → 64×6×16
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
        )
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(64 * 12 * 16, 128),
            nn.ReLU(inplace=True),
            nn.Dropout(0.4),
            nn.Linear(128, 2),
        )

    def forward(self, x):
        return self.classifier(self.features(x))


# ── Training ──────────────────────────────────────────────────────────────────

def train():
    tf = transforms.Compose([
        # ── PIL-space augmentations (before tensor conversion) ────────────────
        # JPEG artifacts: 40–95 quality covers everything from heavy clip
        # compression to a clean screenshot.
        transforms.RandomApply([RandomJpeg(40, 95)], p=0.5),
        # Extra blur: low AA, motion blur, slightly out-of-focus capture.
        RandomBlur(sigma_max=1.5, p=0.25),
        # Brightness/contrast only — NOT saturation or hue, because the
        # kill-feed colour pattern IS the signal; hue shifts would corrupt it.
        transforms.ColorJitter(brightness=0.15, contrast=0.15),
        transforms.RandomHorizontalFlip(),
        # ── Tensor conversion + normalisation ────────────────────────────────
        transforms.ToTensor(),
        transforms.Normalize([0.5, 0.5, 0.5], [0.5, 0.5, 0.5]),
        # Gaussian noise: simulates sensor noise and low-bitrate compression
        # that JPEG doesn't fully capture.
        RandomNoise(std_max=0.04, p=0.5),
    ])

    ds = KillFeedDataset(Path("training_data"), tf)
    val_n = max(1, len(ds) // 10)
    train_ds, val_ds = random_split(ds, [len(ds) - val_n, val_n])

    train_dl = DataLoader(train_ds, batch_size=32, shuffle=True,  num_workers=0)
    val_dl   = DataLoader(val_ds,   batch_size=32, shuffle=False, num_workers=0)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Training on {device}")

    model   = KillNet().to(device)
    opt     = torch.optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-4)
    loss_fn = nn.CrossEntropyLoss()
    sched   = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=30)

    best_acc = 0.0
    for epoch in range(1, 31):
        model.train()
        for imgs, labels in train_dl:
            imgs, labels = imgs.to(device), labels.to(device)
            opt.zero_grad()
            loss_fn(model(imgs), labels).backward()
            opt.step()
        sched.step()

        model.eval()
        correct = total = 0
        with torch.no_grad():
            for imgs, labels in val_dl:
                imgs, labels = imgs.to(device), labels.to(device)
                correct += (model(imgs).argmax(1) == labels).sum().item()
                total   += len(labels)

        acc = correct / total
        marker = " ← best" if acc > best_acc else ""
        print(f"Epoch {epoch:2d}  val acc: {acc:.1%}{marker}")
        if acc > best_acc:
            best_acc = acc
            torch.save(model.state_dict(), "kill_net.pt")

    print(f"\nBest val accuracy: {best_acc:.1%}  →  kill_net.pt")
    export_weights()


# ── C++ header export ─────────────────────────────────────────────────────────
# Writes all layer weights as float arrays so the C++ inference code can load
# them at compile time — zero runtime dependencies, no ONNX needed.

def export_weights(checkpoint="kill_net.pt", out="kill_net_weights.h"):
    model = KillNet()
    model.load_state_dict(torch.load(checkpoint, map_location="cpu"))
    model.eval()

    lines = [
        "// Auto-generated by train.py — do not edit.",
        "#pragma once",
        "#include <array>",
        "",
    ]

    total = 0
    for name, param in model.named_parameters():
        data   = param.detach().cpu().numpy().flatten()
        cname  = name.replace(".", "_")
        values = ", ".join(f"{v:.8f}f" for v in data)
        lines.append(f"// {name}  shape={list(param.shape)}")
        lines.append(f"inline constexpr std::array<float,{len(data)}> kW_{cname} = {{{values}}};")
        lines.append("")
        total += len(data)

    Path(out).write_text("\n".join(lines))
    print(f"Exported {total:,} weights → {out}")


if __name__ == "__main__":
    train()
