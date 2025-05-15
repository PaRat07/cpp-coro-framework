FROM parat07/cpp-default:latest
COPY . .
RUN perf ls && asdasd
CMD ["make", "run-release"]


