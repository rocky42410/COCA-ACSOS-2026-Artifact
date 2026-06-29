# Reproducible build/run environment for the COCA artifact.
# Build:  docker build -t coca-artifact .
# Run  :  docker run --rm coca-artifact            # sample-data repro
#         docker run --rm -v /host/data:/work/data coca-artifact   # your data
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake libgomp1 python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . /work

RUN chmod +x build.sh repro.sh && ./build.sh

CMD ["./repro.sh"]
