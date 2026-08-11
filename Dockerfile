FROM malucart/psx

RUN apk add --no-cache xxd libpng-dev python3 meson ffmpeg-dev

VOLUME .:/project
WORKDIR /project
