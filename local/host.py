import serial
import threading
import os
import time
from fuzzysearch import find_near_matches

_logging_enabled = True
def enable_logging():
    _logging_enabled = True

def _log(msg):
    if _logging_enabled:
        print("HOST: " + msg)

def _byte_to_int(byte):
    return int.from_bytes(byte, "little")

def _bytes_to_int(byte_arr):
    return int.from_bytes(b''.join(byte_arr), "little")

class _MessageParser:
    SYNC_FRAME = [val.to_bytes(1, "little") for val in [97, 98, 99, 100, 101, 102, 103, 104]]
    TIMEOUT = 0.2

    def __init__(self, path, baud, on_ready, on_message, on_timeout):
        self.path = path
        self.baud = baud
        self.on_ready = on_ready
        self.on_message = on_message
        self.on_timeout = on_timeout

        self.data_queue = []
        self.last_msg_time = 0

    def start(self):
        self.running = True
        self.io_thread = threading.Thread(target=self.io)
        self.io_thread.start()
        self.watchdog_thread = threading.Thread(target=self.watchdog)
        self.watchdog_thread.start()

    def stop(self):
        self.running = False
        self.io_thread.join()
        self.watchdog_thread.join()

    def find_sync_frame(self):
        """
        Tries to fuzzy match the first synchronization frame in the queue
        Returns (-1, -1) if there is no match, and returns the range of the sync frame if there is
        """
        matches = find_near_matches(self.SYNC_FRAME, self.data_queue, max_l_dist=2)
        if len(matches) > 0:
            first_match = min(matches, key = lambda match: match.start)
            return (first_match.start, first_match.end)
        else:
            return (-1, -1)

    def io(self):
        while not os.path.exists(self.path):
            time.sleep(1)
        with serial.Serial(self.path, self.baud, timeout=1) as s:
            def send(msg):
                checksum = 0
                for byte in msg:
                    checksum = (checksum + _byte_to_int(byte)) % 256
                s.write(b''.join(msg + [checksum.to_bytes(1, "little")] + self.SYNC_FRAME))

            self.on_ready(send)
            cur = ""
            while self.running:
                byte = s.read()
                self.last_msg_time = time.time()
                self.data_queue.append(byte)
                self.parse()

    def parse(self, allow_abrupt_end = False):
        (start, end) = self.find_sync_frame()
        if start >= 0 and (allow_abrupt_end or len(self.data_queue) > end + 3):
            if start > 0:
                self.on_message(self.data_queue[:start])
            self.data_queue = self.data_queue[end:]

    def watchdog(self):
        while self.running:
            if time.time() - self.last_msg_time > self.TIMEOUT and len(self.data_queue) > 0:
                self.parse(allow_abrupt_end = True)
                self.data_queue = []
                self.on_timeout()
            time.sleep(0.05)

class _MessageProcessor:
    NUM_BLOCKS_MSG = 0x11 # Will also contain the ID of the chip
    CONTACT_DATA_MSG = 0x22
    TEST_STATUS_MSG = 0x33

    NUM_CONTACTS = 16

    def __init__(self, on_blocks):
        self.on_blocks = on_blocks
        self.num_expected_blocks = -1
        self.blocks = {}

    def checksum_valid(self, msg):
        checksum = _byte_to_int(msg[-1])
        msg_only = msg[:-1]
        computed_checksum = 0
        for byte in msg_only:
            computed_checksum = (computed_checksum + _byte_to_int(byte)) % 256
        return checksum == computed_checksum

    def on_message(self, msg):
        if not self.checksum_valid(msg):
            return

        msg_type = _byte_to_int(msg[0])
        msg_data = msg[1 : 1 + 4 + 8 * self.NUM_CONTACTS]
        if msg_type == self.NUM_BLOCKS_MSG:
            self.process_num_blocks_msg(msg_data)
        elif msg_type == self.CONTACT_DATA_MSG:
            self.process_contact_data_msg(msg_data)
        elif msg_type == self.TEST_STATUS_MSG:
            self.process_test_status_msg(msg_data)

    def process_num_blocks_msg(self, msg_data):
        self.num_expected_blocks = _bytes_to_int(msg_data[0:4])
        self.ID = _bytes_to_int(msg_data[4:12])
        _log("Expecting " + str(self.num_expected_blocks) + " blocks")

        if self.num_expected_blocks == 0:
            self.on_blocks(self.ID, self.num_expected_blocks, self.blocks)

    def process_contact_data_msg(self, msg_data):
        if self.num_expected_blocks == -1:
            return

        block_num = _bytes_to_int(msg_data[0:4])
        contacts = []
        for i in range(self.NUM_CONTACTS):
            start = 4 + 8 * i
            end = start + 8
            contacts.append(_bytes_to_int(msg_data[start:end]))

        self.blocks[block_num] = contacts

    def process_test_status_msg(self, msg_data):
        pass

    def on_timeout(self):
        if len(self.blocks) > 0:
            self.on_blocks(self.ID, self.num_expected_blocks, self.blocks)
            self.blocks = {}

class _Protocol:
    TESTING_RESULTS = 0
    CONTACT_DATA = 1

    MAX_REQUEST_SIZE = 32
    TIMEOUT = 0.5

    def __init__(self, on_all_blocks):
        self.on_all_blocks = on_all_blocks

        self.blocks = {}
        self.pending = False

    def start(self):
        self.running = True
        self.wait_thread = threading.Thread(target=self.wait)
        self.wait_thread.start()

    def stop(self):
        self.running = False
        self.wait_thread.join()

    def send_request(self, request_type, blocks=[]):
        _log("Sending request for blocks " + ", ".join([str(num) for num in blocks]))
        self.pending = True
        self.pending_request_type = request_type
        self.pending_blocks = blocks
        self.pending_request_time = time.time();

        data = [
            0xAA, # Indicating that this is a REQUEST
            request_type
        ]
        if len(blocks) == 0:
            data += [0, 0, 0, 0]
        else:
            for n in blocks:
                data += [
                    (n & 0x000000FF) >> 0,
                    (n & 0x0000FF00) >> 8,
                    (n & 0x00FF0000) >> 16,
                    (n & 0xFF000000) >> 24
                ]
        self.send([val.to_bytes(1, "little") for val in data])

    def on_ready(self, send):
        self.send = send
        self.send_request(self.CONTACT_DATA)

    def on_blocks(self, ID, num_blocks, blocks):
        self.pending = False

        for num, data in blocks.items():
            _log("Got block " + str(num))
            self.blocks[num] = data

        missing_blocks = []
        for i in range(num_blocks):
            num = i + 1
            if num not in self.blocks:
                missing_blocks.append(num)

            if len(missing_blocks) == self.MAX_REQUEST_SIZE:
                break

        if missing_blocks:
            _log("Re-requesting blocks " + ", ".join([str(n) for n in missing_blocks]))
            self.send_request(self.CONTACT_DATA, missing_blocks)
        else:
            self.on_all_blocks(ID, self.blocks)

    def wait(self):
        while self.running:
            if self.pending and time.time() - self.pending_request_time > self.TIMEOUT:
                self.send_request(self.pending_request_type, self.pending_blocks)
            time.sleep(0.05)

class Host:
    def __init__(self, port, baud_rate, on_all_blocks):
        self.protocol = _Protocol(on_all_blocks)
        self.processor = _MessageProcessor(self.protocol.on_blocks)
        self.parser = _MessageParser(port, baud_rate, self.protocol.on_ready, self.processor.on_message, self.processor.on_timeout)

    def start(self):
        self.protocol.start()
        self.parser.start()

    def stop(self):
        self.protocol.stop()
        self.parser.stop()
