ARG ROS_DISTRO=rolling
FROM ros:${ROS_DISTRO}-ros-base

SHELL ["/bin/bash", "-c"]

ARG ROS_DISTRO=rolling
ENV ROS_DISTRO=${ROS_DISTRO}
ENV DEBIAN_FRONTEND=noninteractive
ENV RMW_IMPLEMENTATION=rmw_zenoh_cpp

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cargo \
    cmake \
    curl \
    git \
    libboost-dev \
    pkg-config \
    python3 \
    python3-pip \
    rustc \
    ros-${ROS_DISTRO}-rmw-zenoh-cpp \
    ros-${ROS_DISTRO}-std-msgs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/hakoniwa-pdu-endpoint

CMD ["/bin/bash"]
