#https://github.com/sleinen/samplicator
FROM ubuntu:latest
ENV DEBIAN_FRONTEND noninteractive
ENV container docker

# Specify Samplicator Environment Variables
#ENV samplicator_port=1700
#ENV samplicator_arguments='192.168.1.1/1700'

# Open Samplicator Listening Ports
#EXPOSE ${samplicator_port}:${samplicator_port}/udp

# Update Packages
RUN apt-get update && apt-get install -y apt-utils && apt-get -y -f -m --show-progress full-upgrade

# Install Supporting Software
RUN apt-get install -y git cmake make htop wget systemctl gcc curl gpg automake autogen

# Install Samplicator
RUN git clone https://github.com/simeononsecurity/samplicator.git
RUN cd ./samplicator && bash ./autogen.sh
RUN cd /samplicator && ./configure
RUN cd /samplicator && make
RUN cd /samplicator && make install

#RUN cd /samplicator/ && chmod +x ./dockersetup.sh
#RUN cd /samplicator/ && cat ./dockersetup.sh
#CMD ["/bin/bash", "/samplicator/dockersetup.sh"]
ENTRYPOINT ["samplicate"]
