# Stage 1: Build the plugin
FROM gradle:8-jdk17 AS builder
WORKDIR /app
# Copy the plugin source code
COPY windows-xml-parser/ /app/
# Build the JAR (skipping tests to speed up the build)
RUN gradle build -x test

# Stage 2: Data Prepper image
FROM opensearchproject/data-prepper:latest

# Copy Data Prepper configs
COPY data-prepper-config.yaml /usr/share/data-prepper/config/data-prepper-config.yaml
COPY pipelines.yaml /usr/share/data-prepper/pipelines/pipelines.yaml

# Copy custom plugin JARs from the builder stage
COPY --from=builder /app/build/libs/windows-xml-parser-1.0-SNAPSHOT.jar /usr/share/data-prepper/lib/windows-xml-parser-1.0-SNAPSHOT.jar
