cd ~/pico/PicoDVI-pico2_UART/software
rm -fR build
mkdir build
cd build
cmake .. \
  -DPICO_SDK_PATH=/home/noneya/.pico-sdk/sdk/2.1.1 \
  -DPICO_PLATFORM=rp2350 \
  -DPICO_COPY_TO_RAM=1 \
  -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg

make -j$(nproc)
