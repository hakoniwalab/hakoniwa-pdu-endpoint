#!/bin/bash

IMAGE_NAME="hako-pdu-rmw-zenoh"

DOCKER_IMAGE=${IMAGE_NAME}

DOCKER_ID=`docker ps | grep "${DOCKER_IMAGE}" | awk '{print $1}'`

docker exec -it ${DOCKER_ID} /bin/bash
