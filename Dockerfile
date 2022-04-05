#https://github.com/sleinen/samplicator
FROM ubuntu:latest
ENV DEBIAN_FRONTEND noninteractive
ENV container docker

# Specify Samplicator Environment Variables
ENV samplicator_port=1700
ENV samplicator_arguments='192.168.1.1/1700'

# Open Samplicator Listening Ports
EXPOSE ${samplicator_port}:${samplicator_port}

# Update Packages
RUN apt-get update && apt-get -y full-upgrade

# Install Supporting Software
RUN apt-get install -y git cmake make htop wget systemctl gcc curl gpg automake

# Install Samplicator
RUN git clone https://github.com/sleinen/samplicator.git
RUN cd ./samplicator && bash ./autogen.sh
RUN cd /samplicator && ./configure
RUN cd /samplicator && make
RUN cd /samplicator && make install

RUN echo 'samplicate -p ${samplicator_port} ${samplicator_arguments}'  > ./samplicator.sh

CMD ["/bin/bash", "./samplicator.sh"]