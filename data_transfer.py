from random import randbytes
import ok
import msvcrt
import sys
import time
import win32pipe

TIMEOUT_SECONDS = 1
STDIN_HANDLE = msvcrt.get_osfhandle(sys.stdin.fileno())

dev = ok.okCFrontPanel()

BUF_LEN = 16384 # NOTE: length for sending data to the FPGA board over a USB 3.0 connection must be a multiple of 16

# configure the FPGA with the desired bitstream
error = dev.OpenBySerial("2416001B97")
print(f"OpenBySerial ret value: {error}")
error = dev.ConfigureFPGA("C:/Users/rkt23/okClk_pipe/okClk_pipe.runs/impl_1/First.bit")
print(f"ConfigureFPGA ret value: {error}")

# send reset signal to FIFO to initialize it
dev.SetWireInValue(0x10, 0xff, 0x01)
dev.UpdateWireIns()
dev.SetWireInValue(0x10, 0x00, 0x01)
dev.UpdateWireIns()

deadline = time.time() + TIMEOUT_SECONDS
_, data_present, _ = win32pipe.PeekNamedPipe(STDIN_HANDLE, 0)
while time.time() < deadline and data_present == 0:
    try:
        _, data_present, _ = win32pipe.PeekNamedPipe(STDIN_HANDLE, 0)
    except Exception as e:
        print("Exception encountered", e)

dataout = bytearray(BUF_LEN)
while data_present > 0:
    datain = bytearray(sys.stdin.buffer.read(BUF_LEN))
    print(f"Received input data sans newline: [", end="")
    for i in range(len(datain)):
        print(datain[i], end=" ")
    print("]")


    data_present = 0

    # send data to 0x80 endpoint pipe in
    write_ret = dev.WriteToPipeIn(0x80, datain)
    print(f"write to 0x80 ep pipe in return: {write_ret}")

    # read data from 0xA0 endpoint pipe out
    read_ret = dev.ReadFromPipeOut(0xA0, dataout)
    print(f"read from 0xA0 ep pipe out return: {read_ret}")
    print(f"dataout: [", end="")
    for i in range(len(dataout)):
        print(dataout[i], end=" ")
    print("]")

    print("diff: [", end="")
    for i in range(len(datain)):
        print(dataout[i] - datain[i], end=" ")
    print("]")

    deadline = time.time() + TIMEOUT_SECONDS
    while time.time() < deadline and data_present == 0:
        try:
            _, data_present, _ = win32pipe.PeekNamedPipe(STDIN_HANDLE, 0)
            # time.sleep(0.01)
        except Exception as e:
            print("Exception encountered:", e)
            break

print("PYTHON READER: input data timeout")
