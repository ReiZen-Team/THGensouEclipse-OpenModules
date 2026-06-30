#!/bin/python3
import sys
import hashlib
import binascii
import os

SIGNATURE_LENGTH = 128

class Ed25519:
    b = 256
    q = 2**255 - 19
    l = 2**252 + 27742317777372353535851937790883648493
    d = -121665 * pow(121666, -1, q) % q
    I = pow(2, (q - 1) // 4, q)
    By = 4 * pow(5, -1, q) % q
    Bx = None
    B = None

    @classmethod
    def _init(cls):
        if cls.B is not None:
            return
        cls.Bx = cls._xrecover(cls.By)
        cls.B = [cls.Bx, cls.By]

    @staticmethod
    def _H(m):
        return hashlib.sha512(m).digest()

    @classmethod
    def _inv(cls, x):
        return pow(x, cls.q - 2, cls.q)

    @classmethod
    def _xrecover(cls, y):
        yy = (y * y) % cls.q
        numer = (yy - 1) % cls.q
        denom = (cls.d * yy + 1) % cls.q
        xx = (numer * cls._inv(denom)) % cls.q
        x = pow(xx, (cls.q + 3) // 8, cls.q)
        chk = (x * x - xx) % cls.q
        if chk != 0:
            x = (x * cls.I) % cls.q
        if x % 2 != 0:
            x = cls.q - x
        return x

    @classmethod
    def _edwards(cls, P, Q):
        x1, y1 = P
        x2, y2 = Q
        x1x2y1y2 = (x1 * x2 * y1 * y2) % cls.q
        dx1x2y1y2 = (cls.d * x1x2y1y2) % cls.q
        num_x = (x1 * y2 + x2 * y1) % cls.q
        den_x = (1 + dx1x2y1y2) % cls.q
        x3 = (num_x * cls._inv(den_x)) % cls.q
        num_y = (y1 * y2 + x1 * x2) % cls.q
        den_y = (1 - dx1x2y1y2) % cls.q
        y3 = (num_y * cls._inv(den_y)) % cls.q
        return [x3, y3]

    @classmethod
    def _scalarmult(cls, P, e):
        if e == 0:
            return [0, 1]
        Q = cls._scalarmult(P, e // 2)
        Q = cls._edwards(Q, Q)
        if e % 2 == 1:
            Q = cls._edwards(Q, P)
        return Q

    @staticmethod
    def _encodeint(y):
        return y.to_bytes(32, byteorder='little')

    @classmethod
    def _encodepoint(cls, P):
        x, y = P
        bits = bytearray(cls._encodeint(y))
        if x & 1:
            bits[31] |= 0x80
        return bytes(bits)

    @classmethod
    def _Hint(cls, m):
        h = cls._H(m)
        return int.from_bytes(h, byteorder='little')

    @classmethod
    def sign(cls, secret_key_hex, message_content):
        cls._init()
        sk = binascii.unhexlify(secret_key_hex)
        h = cls._H(sk)
        a_bytes = bytearray(h[:32])
        a_bytes[0] &= 248
        a_bytes[31] &= 127
        a_bytes[31] |= 64
        a = int.from_bytes(a_bytes, byteorder='little')
        A = cls._scalarmult(cls.B, a)
        pk_bytes = cls._encodepoint(A)
        r_input = h[32:] + message_content
        r = cls._Hint(r_input)
        R = cls._scalarmult(cls.B, r)
        encR = cls._encodepoint(R)
        hRam = cls._Hint(encR + pk_bytes + message_content)
        S = (r + hRam * a) % cls.l
        sig = encR + cls._encodeint(S)
        return binascii.hexlify(sig).decode('utf-8')

def is_valid_signature(data):
    if len(data) < SIGNATURE_LENGTH:
        return False
    try:
        potential_sig = data[-SIGNATURE_LENGTH:].decode('utf-8')
        binascii.unhexlify(potential_sig)
        return True
    except:
        return False

WASM_MAGIC = b'\x00asm'
SIG_SECTION_NAME = b'signature'

def is_wasm(content):
    return len(content) >= 8 and content[:4] == WASM_MAGIC

def _read_leb(data, pos):
    result = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        result |= (b & 0x7f) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, pos

def _write_leb(n):
    out = bytearray()
    while True:
        b = n & 0x7f
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)

def find_sig_section(content):
    # Byte range (start, end) of the 'signature' custom section, or None.
    if not is_wasm(content):
        return None
    pos = 8
    n = len(content)
    try:
        while pos < n:
            sec_start = pos
            sec_id = content[pos]
            pos += 1
            size, pos = _read_leb(content, pos)
            body_start = pos
            body_end = pos + size
            if body_end > n:
                return None
            if sec_id == 0:
                namelen, np = _read_leb(content, body_start)
                if content[np:np + namelen] == SIG_SECTION_NAME:
                    return (sec_start, body_end)
            pos = body_end
    except Exception:
        return None
    return None

def make_sig_section(sig_hex):
    # 0x00 <leb size> <namelen> "signature" <128 ascii-hex sig>
    payload = sig_hex.encode('ascii')
    body = bytes([len(SIG_SECTION_NAME)]) + SIG_SECTION_NAME + payload
    return b'\x00' + _write_leb(len(body)) + body

def remove_old_signature(content):
    if is_wasm(content):
        sec = find_sig_section(content)
        if sec:
            start, end = sec
            return content[:start] + content[end:]
        return content
    if is_valid_signature(content):
        return content[:-SIGNATURE_LENGTH]
    return content

def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {os.path.basename(sys.argv[0])} [Private_Key_Hex] [File_Path|-]")
        print(f"Note: If Private_Key_Hex is omitted, KMOD_SIGNING_KEY env var is used")
        print(f"      Use '-' for File_Path to read from stdin")
        sys.exit(1)

    env_key = os.environ.get("KMOD_SIGNING_KEY")
    if len(sys.argv) >= 2 and sys.argv[1] != '-' and not os.path.exists(sys.argv[1]):
        secret_key = sys.argv[1]
        file_arg_index = 2
    else:
        secret_key = env_key
        file_arg_index = 1

    if not secret_key:
        print("Error: no signing key. Pass the private key hex as the first "
              "argument or set the KMOD_SIGNING_KEY environment variable.")
        sys.exit(2)

    if len(sys.argv) <= file_arg_index or sys.argv[file_arg_index] == '-':
        content = sys.stdin.buffer.read()
        content = remove_old_signature(content)
        signature_hex = Ed25519.sign(secret_key, content)
        print(f"- {signature_hex}")
        sys.exit(0)

    file_path = sys.argv[file_arg_index]

    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' does not exist.")
        sys.exit(1)

    try:
        with open(file_path, 'rb') as f:
            content = f.read()

        content = remove_old_signature(content)
        signature_hex = Ed25519.sign(secret_key, content)

        with open(file_path, 'wb') as f:
            f.write(content)
            if is_wasm(content):
                # Embed as a WASM custom section — runtimes ignore it, so the
                # module still parses and loads without stripping trailing bytes.
                f.write(make_sig_section(signature_hex))
            else:
                f.write(signature_hex.encode('utf-8'))

        print(f"{file_path}\t{signature_hex}")
        sys.exit(0)

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
