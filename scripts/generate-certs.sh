#!/bin/bash
# Generate self-signed SSL certificates for Ditto Cloud
# Usage: ./generate-certs.sh [domain]

DOMAIN=${1:-"localhost"}
CERTS_DIR="./certs"

echo "=== Ditto Cloud SSL Certificate Generator ==="
echo ""

# Create certs directory
mkdir -p "$CERTS_DIR"

# Generate CA private key
echo "Generating CA private key..."
openssl genrsa -out "$CERTS_DIR/ca.key" 4096 2>/dev/null

# Generate CA certificate
echo "Generating CA certificate..."
openssl req -new -x509 -key "$CERTS_DIR/ca.key" -sha256 -days 365 -out "$CERTS_DIR/ca.crt" -subj "/CN=Ditto Cloud CA/O=Ditto/C=CN"

# Generate server private key
echo "Generating server private key..."
openssl genrsa -out "$CERTS_DIR/server.key" 2048 2>/dev/null

# Generate server CSR
echo "Generating server CSR..."
openssl req -new -key "$CERTS_DIR/server.key" -out "$CERTS_DIR/server.csr" -subj "/CN=$DOMAIN/O=Ditto/C=CN"

# Generate server certificate with SAN
echo "Generating server certificate..."
cat > "$CERTS_DIR/server.ext" <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = $DOMAIN
DNS.2 = *.$DOMAIN
IP.1 = 127.0.0.1
EOF

openssl x509 -req -in "$CERTS_DIR/server.csr" -CA "$CERTS_DIR/ca.crt" -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial -out "$CERTS_DIR/server.crt" -days 365 -sha256 -extfile "$CERTS_DIR/server.ext" 2>/dev/null

# Clean up temp files
rm -f "$CERTS_DIR/server.csr" "$CERTS_DIR/server.ext" "$CERTS_DIR/ca.srl"

echo ""
echo "=== Certificates generated successfully! ==="
echo ""
echo "Files:"
echo "  CA Certificate:  $CERTS_DIR/ca.crt"
echo "  Server Key:      $CERTS_DIR/server.key"
echo "  Server Certificate: $CERTS_DIR/server.crt"
echo ""
echo "To trust the CA on your system:"
echo "  Linux:   sudo cp $CERTS_DIR/ca.crt /usr/local/share/ca-certificates/ && sudo update-ca-certificates"
echo "  Windows: Import ca.crt into 'Trusted Root Certification Authorities'"
echo ""
