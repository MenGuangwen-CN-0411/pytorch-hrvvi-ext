import os
import json
from PIL import Image
from torch.utils.data import Dataset

# https://github.com/pytorch/vision/blob/master/torchvision/datasets/coco.py


class CocoDetection(Dataset):

    def __init__(self, root, ann_file, transform=None):
        from hpycocotools.coco import COCO
        self.root = root
        self.ann_file = ann_file

        with open(self.ann_file, 'r') as f:
            self.data = json.load(f)

        self.coco = COCO(self.data, verbose=False)
        self.ids = list(self.coco.imgs.keys())
        self.transform = transform

    def to_coco(self, indices=None):
        if indices is None:
            return self.data
        ids = [self.ids[i] for i in indices]
        images = self.coco.loadImgs(ids)
        ann_ids = self.coco.getAnnIds(ids)
        annotations = self.coco.loadAnns(ann_ids)
        return {
            **self.data,
            "images": images,
            "annotations": annotations,
        }

    def __getitem__(self, index):
        """
        Args:
            index (int): Index
        Returns:
            tuple: Tuple (image, target). target is the object returned by ``coco.loadAnns``.
        """
        coco = self.coco
        img_id = self.ids[index]
        ann_ids = coco.getAnnIds(imgIds=img_id)
        target = coco.loadAnns(ann_ids)

        path = coco.loadImgs(img_id)[0]['file_name']

        img = Image.open(os.path.join(self.root, path)).convert('RGB')
        if self.transform is not None:
            img, target = self.transform(img, target)

        return img, target

    def __len__(self):
        return len(self.ids)

    def __repr__(self):
        fmt_str = 'Dataset ' + self.__class__.__name__ + '\n'
        fmt_str += '    Number of datapoints: {}\n'.format(self.__len__())
        fmt_str += '    Root Location: {}\n'.format(self.root)
        tmp = '    Transforms (if any): '
        fmt_str += '{0}{1}\n'.format(
            tmp, self.transform.__repr__().replace('\n', '\n' + ' ' * len(tmp)))
        return fmt_str
