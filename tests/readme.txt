Ubuntu 24.04
------------
apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools

gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! x265enc ! rtph265pay ! udpsink host=127.0.0.1 port=5600

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false


Ubuntu 22.04 + NVIDIA
---------------------

sudo apt-get -o Acquire::http::proxy=false gstreamer1.0-tools

gst-inspect-1.0 | grep -i 264
=>
nvcodec:  nvh264dec: NVDEC h264 Video Decoder
nvcodec:  nvh264enc: NVENC H.264 Video Encoder

gst-inspect-1.0 | grep -i 265
=>
nvcodec:  nvh265dec: NVDEC h265 Video Decoder
nvcodec:  nvh265enc: NVENC HEVC Video Encoder


gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! nvh264enc ! rtph264pay ! udpsink host=127.0.0.1 port=5600

gst-launch-1.0 -v udpsrc port=5600 caps = "application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! decodebin ! videoconvert ! queue ! autovideosink


Ubuntu 22.04 (no hardware transcoding)
-------------------------------------

gst-inspect-1.0 | grep -i 264
=>
libav:  avdec_h264: libav H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 decoder
libav:  avenc_h264_omx: libav OpenMAX IL H.264 video encoder encoder

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H264, payload=96 ! rtph264depay ! h264parse ! queue ! avdec_h264 !  videoconvert ! autovideosink sync=false


PPRZ
----
socat -u "/dev/ttyS4",raw,echo=0,b115200 udp-sendto:127.0.0.1:4244
socat -u udp-listen:4245,reuseaddr,fork "/dev/ttyS4",raw,echo=0,b115200 
