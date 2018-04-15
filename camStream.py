#!/usr/bin/python

from picamera import PiCamera, PiVideoFrameType
import socket
import time
from thread import start_new_thread
from threading import RLock
import select


DEBUG = False

#HOST = '192.168.0.139'    # The remote host
PORT = 50007              # The same port as used by the server

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
#s.connect((HOST, PORT))

s.bind(('', PORT))
s.listen(1)


#t = None
#size = 0

class mid():
    def write(self, data):
        #global t, size
        #if t is None:
        #    t = time.time()
        
        print len(data)
        #size += len(data)
        #print (size / (1024*1024)) / (time.time() - t)
        
        conn.send(data)
    
    def close(self):
        pass


#f = s.makefile()
#f = mid()


class Splitter():
    def __init__(self, outFileList):
        self.outFileList = list(outFileList) #copy
        self.lock = RLock()
    
    def removeOutFile(self, f):
        try:
            with self.lock:
                self.outFileList.remove(f)
                f.shutdown(socket.SHUT_RD)
                f.close()
        except Exception as e:
            print e
    
    def write(self, data):
        if DEBUG:
            frame = self.camera.frame
            if frame is not None:
                if (frame.frame_type == PiVideoFrameType.frame):
                    print(frame.index, "frame")
                elif (frame.frame_type == PiVideoFrameType.key_frame):
                    print(frame.index, "key_frame")
                elif (frame.frame_type == PiVideoFrameType.sps_header):
                    print(frame.index, "sps_header")
        
        
        _,outputready,_ = select.select([],self.outFileList,[])
        toBeRemoved = [x for x in self.outFileList if x not in outputready]
        with self.lock:
            for f in outputready:
                try:
                    sentBytes = f.sendall(data)
                except Exception as e:
                    print e
                    toBeRemoved.append(f)
        
        for f in toBeRemoved:
            self.removeOutFile(f)
    
    def flush(self):
        return
        with self.lock:
            for f in self.outFileList:
                try:
                    f.flush()
                except Exception as e:
                    print e
    
    def close(self):
        pass


class CameraHandler():
    def __init__(self):
        self.camera = PiCamera(resolution=(1280, 720), framerate=30, sensor_mode=5)
        self.lock = RLock()
        self.splitter = None
    
    def __del__(self):
        self.camera.close() 
    
    def addOutFile(self, f):
        with self.lock:
            if self.splitter is not None:
                outFileList = list(self.splitter.outFileList)
            else:
                outFileList = []
            outFileList.append(f)
            
            self.splitter = Splitter(outFileList)
            
            if self.camera.recording:
                self.camera.split_recording(self.splitter)
            else:
                self.camera.start_recording(self.splitter, format='h264')
    
    def removeOutFile(self, f):
        try:
            self.splitter.removeOutFile(f)
            with self.lock:
                if len(self.splitter.outFileList) == 0 and self.camera.recording:
                    print("Stopping recording...")
                    self.camera.stop_recording()
        except Exception as e:
            print e
            
            

def client_thread(conn, cam):
    f = conn#.makefile()
    cam.addOutFile(f)
    
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
    except Exception as e:
        print e
    
    print("[-] Closed connection")
    
    cam.removeOutFile(f)
    conn.close()

try:
    c = CameraHandler()
    while True:
        # blocking call, waits to accept a connection
        conn, addr = s.accept()
        print("[-] Connected to " + addr[0] + ":" + str(addr[1]))

        start_new_thread(client_thread, (conn, c,))
finally:
    del c

    s.close()

