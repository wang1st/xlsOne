import os, sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

seed_hex = os.environ.get('SEED_HEX') or (sys.argv[1] if len(sys.argv) > 1 else '')
seed = bytes.fromhex(seed_hex)
key = Ed25519PrivateKey.from_private_bytes(seed)
pem = key.private_bytes(
    encoding=serialization.Encoding.PEM,
    format=serialization.PrivateFormat.PKCS8,
    encryption_algorithm=serialization.NoEncryption(),
)
sys.stdout.write(pem.decode())
