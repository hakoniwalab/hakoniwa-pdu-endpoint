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
    libfastcdr-dev \
    pkg-config \
    python3 \
    python3-pip \
    rustc \
    ros-${ROS_DISTRO}-rmw-zenoh-cpp \
    ros-${ROS_DISTRO}-ros2cli \
    ros-${ROS_DISTRO}-ros2interface \
    ros-${ROS_DISTRO}-std-msgs \
    ros-${ROS_DISTRO}-geometry-msgs \
    ros-${ROS_DISTRO}-sensor-msgs \
    && rm -rf /var/lib/apt/lists/*

COPY docker/scripts/ros2-type-hash /usr/local/bin/ros2-type-hash
RUN chmod +x /usr/local/bin/ros2-type-hash

WORKDIR /workspace/hakoniwa-pdu-endpoint

CMD ["/bin/bash"]
