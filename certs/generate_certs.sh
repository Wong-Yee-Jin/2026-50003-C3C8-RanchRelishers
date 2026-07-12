#!/usr/bin/env bash
# Generates a self-signed cert/key pair for local development TLS.
# Replace with a real CA-issued pair (or your corestack libtetrissh's
# own cert format) for anything beyond local testing.
set -e
cd "$(dirname "$0")"

openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout server.key -out server.crt -days 365 \
  -subj "/C=SG/O=MiniGHTracker/CN=localhost"

echo "Wrote certs/server.crt and certs/server.key"
