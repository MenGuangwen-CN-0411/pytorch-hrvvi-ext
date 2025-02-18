import math

import torch
import torch.nn as nn
import torch.nn.functional as F

from horch.common import _tuple
from horch.models.utils import get_loc_cls_preds
from horch.models.detection.head import SSDHead
from horch.models.modules import Conv2d, get_norm_layer, get_activation, SELayerM, SEModule


class Bottleneck(nn.Module):

    def __init__(self, in_channels, out_channels, stride=1, expansion=4):
        super().__init__()
        self.stride = stride
        self.in_channels = in_channels
        self.out_channels = out_channels
        channels = out_channels // expansion

        self.conv1 = Conv2d(in_channels, channels, kernel_size=1,
                            norm_layer='default', activation='default')
        self.conv2 = Conv2d(channels, channels, kernel_size=3, stride=stride,
                            norm_layer='default', activation='default')

        self.conv3 = Conv2d(channels, out_channels, kernel_size=1,
                            norm_layer='default')
        self.relu3 = get_activation('default')

        self.downsample = None
        if stride != 1 or in_channels != out_channels:
            self.downsample = Conv2d(in_channels, out_channels, kernel_size=1, stride=stride,
                                     norm_layer='default')

    def forward(self, x):
        identity = x
        out = self.conv1(x)
        out = self.conv2(out)
        out = self.conv3(out)
        if self.downsample is not None:
            identity = self.downsample(x)
        out += identity
        out = self.relu3(out)
        return out


class TransferConnection(nn.Module):
    def __init__(self, in_channels, out_channels, last=False):
        super().__init__()
        self.last = last
        self.conv1 = nn.Sequential(
            Conv2d(in_channels, out_channels, kernel_size=5,
                   norm_layer='default', activation='relu', depthwise_separable=True),
            Conv2d(out_channels, out_channels, kernel_size=5,
                   norm_layer='default', depthwise_separable=True),
            SEModule(out_channels, reduction=4),
        )
        if not last:
            self.deconv1 = Conv2d(out_channels, out_channels, kernel_size=4, stride=2,
                                  norm_layer='default', depthwise_separable=True, transposed=True)
        self.nl1 = get_activation('default')
        self.conv2 = Conv2d(
            out_channels, out_channels, kernel_size=5,
            norm_layer='default', activation='default', depthwise_separable=True)

    def forward(self, x, x_next=None):
        x = self.conv1(x)
        if not self.last:
            x = x + self.deconv1(x_next)
        x = self.nl1(x)
        x = self.conv2(x)
        return x


class RefineDet(nn.Module):
    def __init__(self, backbone, num_anchors, num_classes, f_channels, inference, extra_levels=(6,)):
        super().__init__()
        self.num_classes = num_classes
        self.backbone = backbone
        self._inference = inference

        stages = backbone.out_channels

        self.extra_levels = _tuple(extra_levels)
        self.extra_layers = nn.ModuleList([])
        for l in self.extra_levels:
            self.extra_layers.append(
                Bottleneck(stages[-1], f_channels, stride=2)
            )
            stages.append(f_channels)

        self.r_head = SSDHead(num_anchors, 1, stages,
                              norm_layer='default', lite=True)

        self.tcbs = nn.ModuleList([
            TransferConnection(stages[-1], f_channels, last=True)])
        for c in reversed(stages[:-1]):
            self.tcbs.append(
                TransferConnection(c, f_channels, norm_layer='default')
            )

        self.d_head = SSDHead(num_anchors, num_classes, _tuple(f_channels, 3),
                              norm_layer='default', lite=True)

    def forward(self, x):
        cs = self.backbone(x)
        cs = [cs] if torch.is_tensor(cs) else list(cs)
        for l in self.extra_layers:
            cs.append(l(cs[-1]))

        r_loc_p, r_cls_p = self.r_head(*cs)

        dcs = [self.tcbs[0](cs[-1])]
        for c, tcb in zip(reversed(cs[:-1]), self.tcbs[1:]):
            dcs.append(tcb(c, dcs[-1]))
        dcs.reverse()

        d_loc_p, d_cls_p = self.d_head(*dcs)

        return r_loc_p, r_cls_p, d_loc_p, d_cls_p

    def inference(self, x):
        self.eval()
        with torch.no_grad():
            preds = self.forward(x)
        dets = self._inference(*_tuple(preds))
        self.train()
        return dets
