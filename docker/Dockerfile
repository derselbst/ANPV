FROM ubuntu:22.10

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
# this is what we need for running a pipeline in the container
RUN apt-get install -y sudo git

# this is for basic development of ANPV, excluding QT6
RUN apt-get install -y cmake pkg-config build-essential libraw-dev libtiff-dev libjpeg-dev libexiv2-dev

# this is for KDE Framework
RUN apt-get install -y qt6-base-dev qt6-base-dev-tools qt6-image-formats-plugins libqt6svg6-dev libqt6core5compat6-dev qml6-module-qtcore qt6-declarative-dev qt6-base-dev-tools qt6-tools-dev qt6-wayland qt6-wayland-dev qt6-wayland-dev-tools qt6-base-private-dev libxml2-utils docbook-xsl libxslt1-dev libpolkit-gobject-1-dev libpolkit-agent-1-dev libxcb-xkb-dev libxcb-res0-dev libxcb-icccm4-dev libxcb-xfixes0-dev libxcb-record0-dev libxfixes-dev libxcb-keysyms1-dev libwayland-dev libcanberra-dev libgcrypt20-dev libgpgmepp-dev

RUN apt-get install -y x11-xkb-utils libx11-dev libx11-xcb-dev libgpgme-dev

WORKDIR /root
RUN git clone https://invent.kde.org/sdk/kdesrc-build.git
WORKDIR ./kdesrc-build
RUN echo n | ./kdesrc-build --initial-setup

COPY kdesrc-buildrc /root/.config/

RUN ./kdesrc-build +anpv && rm -rf /root/kde
