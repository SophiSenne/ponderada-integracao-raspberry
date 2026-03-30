FROM node:20-alpine AS builder

WORKDIR /app

RUN npm install -g esbuild

COPY k6-test.ts .

RUN esbuild k6-test.ts \
    --bundle \
    --outfile=k6-test.js \
    --format=esm \
    --target=es2015 \
    --platform=neutral \
    --external:k6 \
    --external:k6/*

FROM grafana/k6:latest

WORKDIR /tests

COPY --from=builder /app/k6-test.js .

ENTRYPOINT ["k6"]
CMD ["run", "/tests/k6-test.js"]