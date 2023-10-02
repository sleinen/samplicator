#https://github.com/sleinen/samplicator
FROM ubuntu:latest

LABEL org.opencontainers.image.source="https://github.com/simeononsecurity/samplicator"
LABEL org.opencontainers.image.description="Send copies of (UDP) datagrams to multiple receivers, with optional sampling and spoofing"
LABEL org.opencontainers.image.authors="simeononsecurity"

ENV DEBIAN_FRONTEND noninteractive
ENV container docker

# Specify Samplicator Environment Variables
#ENV samplicator_port=1700
#ENV samplicator_arguments='192.168.1.1/1700'

# Open Samplicator Listening Ports
#EXPOSE ${samplicator_port}:${samplicator_port}/udp

# Update Packages
RUN apt-get update && apt-get install -y --no-install-recommends apt-utils && apt-get -y -f -m --show-progress --no-install-recommends full-upgrade

# Install Supporting Software
RUN apt-get install -y --no-install-recommends git cmake make htop wget systemctl gcc curl gpg automake autogen

# Install Samplicator
RUN git clone https://github.com/simeononsecurity/samplicator.git
RUN cd ./samplicator && bash ./autogen.sh
RUN cd /samplicator && ./configure
RUN cd /samplicator && make
RUN cd /samplicator && make install

#RUN cd /samplicator/ && chmod +x ./dockersetup.sh
#RUN cd /samplicator/ && cat ./dockersetup.sh
#CMD ["/bin/bash", "/samplicator/dockersetup.sh"]

# Clean APT
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["samplicate"]
