FROM vitasdk/vitasdk:latest

ENV VITASDK=/usr/local/vitasdk
ENV PATH="${VITASDK}/bin:${PATH}"

USER root

# VitaSDK supplies the target libraries; install the build and formatting tools.
RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential cmake git python3 python3-pil xxhash ca-certificates pkg-config clang-format && \
    rm -rf /var/lib/apt/lists/*

# Use the unprivileged account provided by the VitaSDK image.
USER vitasdk
