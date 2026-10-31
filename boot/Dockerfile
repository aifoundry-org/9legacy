# The image of boot/image: the sources of the tree and one userland
# built from them, in /9legacy, served over 9P on port 564 by the
# tree's own u9fs(4) as boot/qemu -u9fs serves it, with no
# authentication and every attach as glenda.
ARG DEBIAN=debian:trixie-slim

FROM ${DEBIAN} AS u9fs
RUN apt-get update -q && apt-get install -qy --no-install-recommends gcc libc6-dev make
COPY tree/sys/src/cmd/unix/u9fs /src/u9fs
RUN make -C /src/u9fs CFLAGS='-O2 -I. -D_GNU_SOURCE' && strip /src/u9fs/u9fs

FROM ${DEBIAN}
RUN apt-get update -q && apt-get install -qy --no-install-recommends socat \
 && rm -rf /var/lib/apt/lists/* && useradd -M -d /9legacy -u 1000 glenda
COPY --from=u9fs /src/u9fs/u9fs /usr/local/bin/
COPY --chown=glenda:glenda tree/ /9legacy/
USER glenda
EXPOSE 564
ENTRYPOINT ["socat", "TCP-LISTEN:564,reuseaddr,fork", "EXEC:/usr/local/bin/u9fs -n -a none -u glenda -l /tmp/u9fs.log /9legacy"]
