#! /usr/bin/python3
import cgi, cgitb
import os, mmap, struct

cgitb.enable()

# ---- Constants ----
RADIO_TUNER_FAKE_ADC_PINC_OFFSET = 0  # word offsets (32-bit)
RADIO_TUNER_TUNER_PINC_OFFSET    = 1
RADIO_TUNER_CONTROL_REG_OFFSET   = 2
RADIO_TUNER_TIMER_REG_OFFSET     = 3

RADIO_PERIPH_ADDRESS = 0x43C00000
FIFO_Enable_GPIO     = 0x41200000
MAP_SIZE             = 4096

FSYS_HZ    = 125e6
PHASE_BITS = 27

# ---- /dev/mem helper ----
class Mem32:
    def __init__(self, phys_addr: int, size: int = MAP_SIZE):
        self.fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(self.fd, size,
                            flags=mmap.MAP_SHARED,
                            prot=mmap.PROT_READ | mmap.PROT_WRITE,
                            offset=phys_addr)

    def close(self):
        try:
            self.mm.close()
        finally:
            os.close(self.fd)

    def write_u32(self, word_off: int, value: int):
        self.mm.seek(word_off * 4)
        self.mm.write(struct.pack("<I", value & 0xFFFFFFFF))

    def read_u32(self, word_off: int) -> int:
        self.mm.seek(word_off * 4)
        return struct.unpack("<I", self.mm.read(4))[0]

# ---- Phase increment helpers ----
def phase_inc_for_adc(freq_hz: float) -> int:
    # (int)( +freq * 2^27 / 125e6 )
    return int(freq_hz * (1 << PHASE_BITS) / FSYS_HZ)

def phase_inc_for_tuner(tune_freq_hz: float) -> int:
    # (int)( -tune_freq * 2^27 / 125e6 )
    return int(-tune_freq_hz * (1 << PHASE_BITS) / FSYS_HZ)

# ---- CGI input ----
form = cgi.FieldStorage()

def get_float(name, default=0.0):
    v = form.getfirst(name)
    try:
        return float(v)
    except (TypeError, ValueError):
        return default

adc_freq_hz  = get_float('adc_freq_hz', 0.0)
tune_freq_hz = get_float('tune_freq_hz', 0.0)
streaming    = form.getfirst('streaming', '')  # "streaming" if checked

# Compute phase inputs
adc_pinc  = phase_inc_for_adc(adc_freq_hz)
tune_pinc = phase_inc_for_tuner(tune_freq_hz)

# ---- Send the CGI header FIRST ----
print("Content-type: text/html")
print()
print("<html>")
print("<head><title>Radio Configurator</title></head>")
print("<body>")
print("<h2>Radio Configurator</h2>")
print("Setting up the radio now...<br>")
print(f"ADC Freq = {adc_freq_hz:.3f} Hz, Tune Freq = {tune_freq_hz:.3f} Hz<br>")
print(f"Computed ADC phase_input = {adc_pinc} (0x{adc_pinc & 0xFFFFFFFF:08X})<br>")
print(f"Computed Tuner phase_input = {tune_pinc} (0x{tune_pinc & 0xFFFFFFFF:08X})<br>")
print(f"Streaming requested: {'Enabled' if streaming == 'streaming' else 'Disabled'}<br>")

# ---- Try to write hardware registers ----
error = None
readback_adc = readback_tune = None

try:
    regs  = Mem32(RADIO_PERIPH_ADDRESS)
    regs2 = Mem32(FIFO_Enable_GPIO)
    try:
        regs.write_u32(RADIO_TUNER_FAKE_ADC_PINC_OFFSET, adc_pinc)
        regs.write_u32(RADIO_TUNER_TUNER_PINC_OFFSET,    tune_pinc)

        # GPIO: enable/disable streaming at word offset 0
        if streaming == "streaming":
            regs2.write_u32(0, 0x00000001)
        else:
            regs2.write_u32(0, 0x00000000)

        # Read back
        readback_adc  = regs.read_u32(RADIO_TUNER_FAKE_ADC_PINC_OFFSET)
        readback_tune = regs.read_u32(RADIO_TUNER_TUNER_PINC_OFFSET)
    finally:
        regs2.close()
        regs.close()
except Exception as e:
    error = str(e)

# ---- HTML footer / results ----
if error:
    print(f"<p style='color:red;'>ERROR: {error}</p>")
else:
    print("<h3>Register Readback</h3>")
    print(f"Fake ADC PINC = 0x{readback_adc:08X}<br>")
    print(f"Tuner PINC    = 0x{readback_tune:08X}<br>")
    print(f"Streaming is <b>{'Enabled' if streaming == 'streaming' else 'Disabled'}</b><br>")

print("</body></html>")
