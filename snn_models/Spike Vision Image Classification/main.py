import os
import copy
import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, transforms
from torchvision.models.quantization import shufflenet_v2_x0_5
from torch.utils.data import DataLoader

import warnings

warnings.filterwarnings("ignore")

BATCH_SIZE = 32
NUM_CLASSES = 2
DATA_DIR = "./datasets_hand"
QUANT_BACKEND = "fbgemm"
torch.backends.quantized.engine = QUANT_BACKEND
DEVICE = torch.device("cpu")


def get_dataloaders():
    data_transforms = {
        'train': transforms.Compose([
            transforms.Resize((224, 224)),
            transforms.RandomHorizontalFlip(),
            transforms.RandomRotation(15),
            transforms.ColorJitter(brightness=0.3, contrast=0.3, saturation=0.3, hue=0.1),
            transforms.ToTensor(),
            transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
        ]),
        'val': transforms.Compose([
            transforms.Resize((224, 224)),
            transforms.ToTensor(),
            transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
        ])
    }
    image_datasets = {x: datasets.ImageFolder(os.path.join(DATA_DIR, x), data_transforms[x]) for x in ['train', 'val']}
    dataloaders = {x: DataLoader(image_datasets[x], batch_size=BATCH_SIZE, shuffle=(x == 'train'), num_workers=2) for x
                   in ['train', 'val']}
    dataset_sizes = {x: len(image_datasets[x]) for x in ['train', 'val']}
    return dataloaders, dataset_sizes


def train_model(model, dataloaders, dataset_sizes, criterion, optimizer, num_epochs):
    best_model_wts = copy.deepcopy(model.state_dict())
    best_acc = 0.0

    for epoch in range(num_epochs):
        for phase in ['train', 'val']:
            if phase == 'train':
                model.train()
            else:
                model.eval()

            running_loss = 0.0
            running_corrects = 0

            for inputs, labels in dataloaders[phase]:
                inputs, labels = inputs.to(DEVICE), labels.to(DEVICE)
                optimizer.zero_grad()

                with torch.set_grad_enabled(phase == 'train'):
                    outputs = model(inputs)
                    _, preds = torch.max(outputs, 1)
                    loss = criterion(outputs, labels)

                    if phase == 'train':
                        loss.backward()
                        optimizer.step()

                running_loss += loss.item() * inputs.size(0)
                running_corrects += torch.sum(preds == labels.data)

            epoch_loss = running_loss / dataset_sizes[phase]
            epoch_acc = running_corrects.double() / dataset_sizes[phase]

            print(f'Epoch {epoch + 1:2d}/{num_epochs} | {phase:5s} | Loss: {epoch_loss:.4f} Acc: {epoch_acc:.4f}')

            if phase == 'val' and epoch_acc > best_acc:
                best_acc = epoch_acc
                best_model_wts = copy.deepcopy(model.state_dict())

    model.load_state_dict(best_model_wts)
    return model


if __name__ == '__main__':
    dataloaders, dataset_sizes = get_dataloaders()

    model_fp32 = shufflenet_v2_x0_5(weights="DEFAULT", quantize=False)
    in_features = model_fp32.fc.in_features

    model_fp32.fc = nn.Sequential(
        nn.Dropout(p=0.5),
        nn.Linear(in_features, NUM_CLASSES)
    )
    model_fp32 = model_fp32.to(DEVICE)

    criterion = nn.CrossEntropyLoss()

    optimizer_fp32 = optim.Adam(model_fp32.parameters(), lr=0.001)
    model_fp32 = train_model(model_fp32, dataloaders, dataset_sizes, criterion, optimizer_fp32, num_epochs=10)

    model_fp32.eval()
    model_fp32.fuse_model()
    model_fp32.train()

    model_fp32.qconfig = torch.ao.quantization.get_default_qat_qconfig(QUANT_BACKEND)
    model_qat = torch.ao.quantization.prepare_qat(model_fp32, inplace=False).to(DEVICE)

    optimizer_qat = optim.Adam(model_qat.parameters(), lr=0.0001)
    best_qat_model = train_model(model_qat, dataloaders, dataset_sizes, criterion, optimizer_qat, num_epochs=10)

    best_qat_model.eval()
    model_int8 = torch.ao.quantization.convert(best_qat_model, inplace=False)

    dummy_input = torch.randn(1, 3, 224, 224)
    scripted_model = torch.jit.trace(model_int8, dummy_input)
    scripted_model.save("best_shufflenet.pt")

    print("\n[Success] Saved to best_shufflenet.pt")