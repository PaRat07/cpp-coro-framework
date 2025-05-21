FROM parat07/cpp-default:latest

COPY . .
RUN make build-release
CMD ["make", "run-release"]


