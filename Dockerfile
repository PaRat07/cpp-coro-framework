FROM parat07/cpp-default:latest
RUN apt install -y auditd
RUN auditctl -e 0
RUN auditctl -a never,task
RUN systemctl stop iptables &&\
    systemctl disable iptables


#CMD ["make", "run-release"]


