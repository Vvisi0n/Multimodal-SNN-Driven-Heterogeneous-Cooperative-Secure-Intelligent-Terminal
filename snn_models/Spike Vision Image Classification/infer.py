import os
import torch
from torchvision import datasets, transforms
from torch.utils.data import DataLoader

DATA_DIR = "./mydata/test"  # 测试集根目录
MODEL_PATH = "best_shufflenet.pt"
BATCH_SIZE = 32

DEVICE = torch.device("cpu")
model = torch.jit.load(MODEL_PATH, map_location=DEVICE)
model.eval()

transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
])

test_dataset = datasets.ImageFolder(DATA_DIR, transform)
test_loader = DataLoader(test_dataset, batch_size=BATCH_SIZE, shuffle=False)

correct = 0
total = 0

with torch.no_grad():
    for inputs, labels in test_loader:
        inputs, labels = inputs.to(DEVICE), labels.to(DEVICE)
        outputs = model(inputs)
        _, preds = torch.max(outputs, 1)

        correct += torch.sum(preds == labels.data).item()
        total += labels.size(0)

print(f"测试集路径: {DATA_DIR}")
print(f"样本总数: {total}")
print(f"准确率 (Accuracy): {correct / total:.4f}")