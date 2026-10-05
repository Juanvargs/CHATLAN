"""Prueba cruzada con el protocolo y la identidad del cliente Python original."""
import asyncio
import os
import ssl
import sys
import tempfile
import uuid
from pathlib import Path

original = Path(os.environ.get("CHAT_LAN_PYTHON_ORIGINAL", Path(__file__).resolve().parents[3] / "chat-server"))
source = original / "src"
if not source.is_dir():
    raise SystemExit("Falta el proyecto Python original; indica su ruta con CHAT_LAN_PYTHON_ORIGINAL")
sys.path.insert(0, str(source))
from chat_lan.client.identity import Identity
from chat_lan.common.protocol import b64, encode, event, proof_payload, receive


async def main(certificate, port):
    with tempfile.TemporaryDirectory() as profile:
        identity = Identity(Path(profile))
        ctx = ssl.create_default_context(cafile=certificate)
        async def connect():
            reader, writer = await asyncio.open_connection("127.0.0.1", port, ssl=ctx)
            writer.write(encode(event("hello", name="Compatibilidad Python", public=identity.public)))
            challenge = await receive(reader, 5)
            assert challenge["type"] == "challenge", challenge
            proof = b64(identity.key.sign(proof_payload(challenge["nonce"], identity.public, challenge["fingerprint"])))
            writer.write(encode(event("proof", signature=proof)))
            while (message := await receive(reader, 5))["type"] != "authenticated":
                assert message["type"] not in {"error", "revoked", "registration_pending"}, message
            return reader, writer

        reader, writer = await connect()
        identifier = str(uuid.uuid4())
        writer.write(encode(event("send_message", message_id=identifier, to="general", text="Hola desde Python")))
        while (message := await receive(reader, 5))["type"] != "accepted":
            assert message["type"] != "error", message
        assert message["message_id"] == identifier and message["duplicate"] is False
        writer.close()
        await writer.wait_closed()
        reader, writer = await connect()
        for _ in range(20):
            message = await receive(reader, 5)
            assert message["type"] != "error", message
            if message["type"] == "message" and message["message_id"] == identifier:
                assert message["history"] is True
                break
        else:
            raise AssertionError("El historial no incluyó el mensaje enviado")
        writer.close()
        await writer.wait_closed()
        print("Cliente Python autenticado; mensaje aceptado e historial recuperado")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], int(sys.argv[2])))
