################################################################################
# SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#################################################################################

CUDA_VER?=
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set")
endif
TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)
CC:= gcc
SRCS:= gstfacetracker.c
INCS:= $(wildcard *.h)
LIB:=libgstfacetracker.so

CFLAGS+= -fPIC -DDS_VERSION=\"7.0.0\" \
	 -I /usr/local/cuda-$(CUDA_VER)/include \
	 -I /opt/nvidia/deepstream/deepstream/sources/includes

GST_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream/lib/gst-plugins/
LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream/lib/

ifeq ($(TARGET_DEVICE),aarch64)
        LIBS := -L/usr/lib/aarch64-linux-gnu/tegra
endif

LIBS+= -shared -Wl,-no-undefined \
	-L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart

LIBS+= -L$(LIB_INSTALL_DIR) -lnvdsgst_helper -lnvdsgst_meta -lnvds_meta -lnvdsbufferpool \
       -lnvbufsurface -lnvbufsurftransform -lnvds_csvparser -lnvds_dewarper \
			 -lnvds_nvtxhelper -ldl -lpthread -lcuda -Wl,-rpath,$(LIB_INSTALL_DIR)

OBJS:= $(SRCS:.c=.o)

PKGS:= gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0
CFLAGS+=$(shell pkg-config --cflags $(PKGS))
LIBS+=$(shell pkg-config --libs $(PKGS))

all: $(LIB)

%.o: %.c $(INCS) Makefile
	$(CC) -c -o $@ $(CFLAGS) $<

$(LIB): $(OBJS) $(DEP) Makefile
	$(CC) -o $@ $(OBJS) $(LIBS)

install: $(LIB)
	cp -rv $(LIB) $(GST_INSTALL_DIR)

install-strip: $(LIB)
	cp -rv $(LIB) $(GST_INSTALL_DIR)

clean:
	rm -rf $(OBJS) $(LIB)
