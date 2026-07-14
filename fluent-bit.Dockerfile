FROM fluent/fluent-bit:3.2

# Copy configuration and scripts
COPY fluent-bit.conf /fluent-bit/etc/fluent-bit.conf
COPY fluent-bit-scripts/ /fluent-bit/scripts/
