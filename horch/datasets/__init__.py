import numpy as np
from torch.utils.data import Dataset
from torchvision.transforms import Compose
from horch.transforms import InputTransform

from horch.datasets.captcha import Captcha, CaptchaDetectionOnline, CaptchaOnline, CaptchaSegmentationOnline
from horch.datasets.coco import CocoDetection
from horch.datasets.voc import VOCDetection, VOCSegmentation
from horch.datasets.svhn import SVHNDetection


class Fullset(Dataset):

    def __init__(self, dataset, transform):
        self.dataset = dataset
        self.transform = transform

    def __getitem__(self, idx):
        input, target = self.dataset[idx]
        return self.transform(input, target)

    def __len__(self):
        return len(self.dataset)

    # def __getattr__(self, attr):
    #     return getattr(self.dataset, attr)

    def __repr__(self):
        return "Fullset(%s)" % self.dataset


class Subset(Dataset):
    """
    Subset of a dataset at specified indices.

    Arguments:
        dataset (Dataset): The whole Dataset
        indices (sequence): Indices in the whole set selected for subset
    """

    def __init__(self, dataset, indices, transform=None):
        self.dataset = dataset
        self.indices = indices
        self.transform = transform

    def __getitem__(self, idx):
        img, target = self.dataset[self.indices[idx]]

        if self.transform is not None:
            return self.transform(img, target)

        return img, target

    def __len__(self):
        return len(self.indices)

    def to_coco(self, indices=None):
        assert hasattr(self.dataset, "to_coco"), "Dataset don't support to_coco"
        if indices is None:
            indices = self.indices
        else:
            indices = [self.indices[i] for i in indices]
        return self.dataset.to_coco(indices)

    # def __getattr__(self, attr):
    #     return getattr(self.dataset, attr)

    def __repr__(self):
        fmt_str = 'Subset of ' + self.dataset.__class__.__name__ + '\n'
        fmt_str += '    Number of datapoints: {}\n'.format(self.__len__())
        return fmt_str


def train_test_split(dataset, test_ratio, random=False, transform=None, test_transform=None):
    if isinstance(transform, Compose):
        transform = InputTransform(transform)
    if isinstance(test_transform, Compose):
        test_transform = InputTransform(test_transform)
    num_examples = len(dataset)
    num_test_examples = int(num_examples * test_ratio)
    num_train_examples = num_examples - num_test_examples
    if random:
        indices = np.random.permutation(num_examples)
        train_indices = indices[:num_train_examples]
        test_indices = indices[num_train_examples:]
    else:
        train_indices = np.arange(num_train_examples)
        test_indices = np.arange(num_train_examples, num_examples)
    train_set = Subset(
        dataset, train_indices, transform)
    test_set = Subset(
        dataset, test_indices, test_transform)
    return train_set, test_set


class CachedDataset(Dataset):

    def __init__(self, dataset):
        self.dataset = dataset
        self.cache = [None] * len(dataset)

    def __len__(self):
        return len(self.dataset)

    def __getitem__(self, idx):
        if self.cache[idx] is None:
            self.cache[idx] = self.dataset[idx]
        return self.cache[idx]
