// HpPrimeUsbClient.cs
// Requires: NuGet package "MadWizard.WinUSBNet" or similar WinUSB wrapper
// Or use raw P/Invoke as shown below

using System;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HpPrimeUsb
{
    #region Protocol Definitions

    public static class UsbProto
    {
        public const int PacketSize = 64;
        public const int PayloadSize = 60;
        public const int MaxTransferSize = 4 * 1024 * 1024;

        // Commands (Host -> Device)
        public const byte CMD_PING = 0x01;
        public const byte CMD_READ_FILE = 0x10;
        public const byte CMD_WRITE_FILE = 0x11;
        public const byte CMD_LIST_DIR = 0x12;
        public const byte CMD_DELETE_FILE = 0x13;
        public const byte CMD_MKDIR = 0x14;
        public const byte CMD_FILE_INFO = 0x15;
        public const byte CMD_READ_MEM = 0x20;
        public const byte CMD_WRITE_MEM = 0x21;
        public const byte CMD_LOG_READ = 0x30;
        public const byte CMD_KEY_INJECT = 0x40;

        // Responses (Device -> Host)
        public const byte RSP_OK = 0x80;
        public const byte RSP_ERROR = 0x81;
        public const byte RSP_DATA = 0x82;
        public const byte RSP_DATA_END = 0x83;
        public const byte RSP_PONG = 0x84;
        public const byte RSP_DIR_ENTRY = 0x85;
        public const byte RSP_DIR_END = 0x86;

        // Errors
        public const byte ERR_NONE = 0x00;
        public const byte ERR_UNKNOWN_CMD = 0x01;
        public const byte ERR_FILE_NOT_FOUND = 0x02;
        public const byte ERR_FILE_OPEN_FAIL = 0x03;
        public const byte ERR_FILE_IO = 0x04;
        public const byte ERR_BAD_PARAM = 0x05;
        public const byte ERR_NO_MEM = 0x06;
        public const byte ERR_TOO_LARGE = 0x07;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct UsbPacket
    {
        public byte Cmd;
        public byte Seq;
        public ushort PayloadLen;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 60)]
        public byte[] Data;

        public static UsbPacket Create(byte cmd, byte seq)
        {
            return new UsbPacket
            {
                Cmd = cmd,
                Seq = seq,
                PayloadLen = 0,
                Data = new byte[60]
            };
        }

        public byte[] ToBytes()
        {
            byte[] result = new byte[64];
            result[0] = Cmd;
            result[1] = Seq;
            BitConverter.GetBytes(PayloadLen).CopyTo(result, 2);
            if (Data != null)
                Array.Copy(Data, 0, result, 4, Math.Min(Data.Length, 60));
            return result;
        }

        public static UsbPacket FromBytes(byte[] raw)
        {
            var pkt = new UsbPacket();
            pkt.Cmd = raw[0];
            pkt.Seq = raw[1];
            pkt.PayloadLen = BitConverter.ToUInt16(raw, 2);
            pkt.Data = new byte[60];
            Array.Copy(raw, 4, pkt.Data, 0, 60);
            return pkt;
        }
    }

    public class DirEntryInfo
    {
        public uint Attributes { get; set; }
        public uint FileSize { get; set; }
        public string Name { get; set; }
        public bool IsDirectory => (Attributes & 0x10) != 0;

        public override string ToString()
        {
            string type = IsDirectory ? "<DIR>" : $"{FileSize,10}";
            return $"{type}  {Name}";
        }
    }

    #endregion

    #region Transport Abstraction

    public interface IUsbTransport : IDisposable
    {
        void Send(byte[] packet64);
        byte[] Receive(int timeoutMs = 5000);
    }

    /// <summary>
    /// Real WinUSB transport using DeviceIoControl (matching HP Prime's protocol)
    /// </summary>
    public class WinUsbTransport : IUsbTransport
    {
        private IntPtr _handle;
        private const uint IOCTL_SEND = 0x00000102;  // 258 decimal
        private const uint IOCTL_RECV = 0x00000103;  // 259 decimal (assumed)

        // HP Prime USB GUID - may need adjustment
        private static readonly Guid HP_PRIME_GUID =
            new Guid(0x12d1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); // placeholder

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr CreateFileW(
            [MarshalAs(UnmanagedType.LPWStr)] string lpFileName,
            uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes,
            uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool DeviceIoControl(
            IntPtr hDevice, uint dwIoControlCode,
            byte[] lpInBuffer, uint nInBufferSize,
            byte[] lpOutBuffer, uint nOutBufferSize,
            out uint lpBytesReturned, IntPtr lpOverlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("setupapi.dll", SetLastError = true)]
        static extern IntPtr SetupDiGetClassDevs(
            ref Guid ClassGuid, IntPtr Enumerator, IntPtr hwndParent, uint Flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        static extern bool SetupDiEnumDeviceInterfaces(
            IntPtr DeviceInfoSet, IntPtr DeviceInfoData,
            ref Guid InterfaceClassGuid, uint MemberIndex,
            ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData);

        [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool SetupDiGetDeviceInterfaceDetail(
            IntPtr DeviceInfoSet,
            ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
            IntPtr DeviceInterfaceDetailData,
            uint DeviceInterfaceDetailDataSize,
            out uint RequiredSize,
            IntPtr DeviceInfoData);

        [DllImport("setupapi.dll")]
        static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

        [StructLayout(LayoutKind.Sequential)]
        struct SP_DEVICE_INTERFACE_DATA
        {
            public uint cbSize;
            public Guid InterfaceClassGuid;
            public uint Flags;
            public IntPtr Reserved;
        }

        const uint DIGCF_PRESENT = 0x02;
        const uint DIGCF_DEVICEINTERFACE = 0x10;
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x40000000;
        const uint OPEN_EXISTING = 3;

        public WinUsbTransport(string devicePath = null)
        {
            if (devicePath == null)
                devicePath = FindHpPrimeDevice();

            if (devicePath == null)
                throw new InvalidOperationException("HP Prime not found");

            _handle = CreateFileW(devicePath,
                GENERIC_READ | GENERIC_WRITE, 0, IntPtr.Zero,
                OPEN_EXISTING, 0, IntPtr.Zero);

            if (_handle == (IntPtr)(-1))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }

        private string FindHpPrimeDevice()
        {
            // Search for HP Prime USB device
            // VID=0x03F0 (HP), PID varies by firmware
            // This is a simplified search - real implementation would enumerate
            // all USB devices and match VID/PID
            var guid = HP_PRIME_GUID;
            var devInfo = SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero,
                DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

            if (devInfo == (IntPtr)(-1)) return null;

            try
            {
                var ifData = new SP_DEVICE_INTERFACE_DATA();
                ifData.cbSize = (uint)Marshal.SizeOf(ifData);

                if (!SetupDiEnumDeviceInterfaces(devInfo, IntPtr.Zero, ref guid, 0, ref ifData))
                    return null;

                SetupDiGetDeviceInterfaceDetail(devInfo, ref ifData, IntPtr.Zero, 0,
                    out uint requiredSize, IntPtr.Zero);

                IntPtr detailData = Marshal.AllocHGlobal((int)requiredSize);
                try
                {
                    // Set cbSize for the detail struct (varies by arch)
                    Marshal.WriteInt32(detailData, IntPtr.Size == 8 ? 8 : 5);

                    if (SetupDiGetDeviceInterfaceDetail(devInfo, ref ifData, detailData,
                        requiredSize, out _, IntPtr.Zero))
                    {
                        return Marshal.PtrToStringUni(detailData + 4);
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(detailData);
                }
            }
            finally
            {
                SetupDiDestroyDeviceInfoList(devInfo);
            }

            return null;
        }

        public void Send(byte[] packet64)
        {
            if (packet64.Length != 64)
                throw new ArgumentException("Packet must be 64 bytes");

            for (int retry = 0; retry < 5; retry++)
            {
                if (DeviceIoControl(_handle, IOCTL_SEND, packet64, 64,
                    null, 0, out _, IntPtr.Zero))
                    return;

                Thread.Sleep(1);
            }
            throw new IOException("Failed to send USB packet after 5 retries");
        }

        public byte[] Receive(int timeoutMs = 5000)
        {
            byte[] result = new byte[64];
            var sw = System.Diagnostics.Stopwatch.StartNew();

            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                if (DeviceIoControl(_handle, IOCTL_RECV, null, 0,
                    result, 64, out uint bytesReturned, IntPtr.Zero))
                {
                    if (bytesReturned >= 64)
                        return result;
                }
                Thread.Sleep(1);
            }
            throw new TimeoutException($"USB receive timed out after {timeoutMs}ms");
        }

        public void Dispose()
        {
            if (_handle != IntPtr.Zero && _handle != (IntPtr)(-1))
            {
                CloseHandle(_handle);
                _handle = IntPtr.Zero;
            }
        }
    }

    /// <summary>
    /// Named pipe transport for debugging/testing without hardware
    /// </summary>
    public class PipeTransport : IUsbTransport
    {
        private readonly Stream _sendStream;
        private readonly Stream _recvStream;
        private readonly bool _isServer;

        /// <summary>
        /// Create a pipe transport. If isServer=true, creates named pipe servers.
        /// If isServer=false, connects to existing pipes.
        /// </summary>
        public PipeTransport(string pipeName = "primu", bool isServer = false)
        {
            _isServer = isServer;
            string sendPipe = pipeName;
            string recvPipe = pipeName;

            if (isServer)
            {
                // Server side (simulates the device)
                var sendServer = new NamedPipeServerStream(recvPipe,
                    PipeDirection.Out, 1, PipeTransmissionMode.Byte);
                var recvServer = new NamedPipeServerStream(sendPipe,
                    PipeDirection.In, 1, PipeTransmissionMode.Byte);

                Console.WriteLine("Pipe server waiting for connections...");
                // Accept connections (order matters - must match client)
                Task.WaitAll(
                    Task.Run(() => recvServer.WaitForConnection()),
                    Task.Run(() => sendServer.WaitForConnection())
                );
                Console.WriteLine("Pipe server connected.");

                _sendStream = sendServer;
                _recvStream = recvServer;
            }
            else
            {
                var recvClient = new NamedPipeClientStream(".", recvPipe,
                    PipeDirection.InOut);

                recvClient.Connect(5000);
                _recvStream = recvClient;
            }
        }

        public void Send(byte[] packet64)
        {
            if (packet64.Length != 64)
                throw new ArgumentException("Packet must be 64 bytes");
            _recvStream.Write(packet64, 0, 64);
            _recvStream.Flush();
        }

        public byte[] Receive(int timeoutMs = 5000)
        {
            byte[] buf = new byte[64];
            int totalRead = 0;

            // Set up a cancellation for timeout
            using var cts = new CancellationTokenSource(timeoutMs);

            while (totalRead < 64)
            {
                if (cts.IsCancellationRequested)
                    throw new TimeoutException("Pipe receive timed out");

                int read = _recvStream.Read(buf, totalRead, 64 - totalRead);
                if (read == 0)
                    throw new IOException("Pipe closed");
                totalRead += read;
            }
            return buf;
        }

        public void Dispose()
        {
            _sendStream?.Dispose();
            _recvStream?.Dispose();
        }
    }

    #endregion

    #region High-Level Client API

    public class HpPrimeClient : IDisposable
    {
        private readonly IUsbTransport _transport;
        private byte _seq = 0;
        private readonly object _lock = new object();

        public HpPrimeClient(IUsbTransport transport)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        }

        private byte NextSeq() => _seq++;

        private UsbPacket SendAndReceive(UsbPacket pkt, int timeoutMs = 5000)
        {
            lock (_lock)
            {
                _transport.Send(pkt.ToBytes());
                return UsbPacket.FromBytes(_transport.Receive(timeoutMs));
            }
        }

        private UsbPacket SendRaw(UsbPacket pkt)
        {
            _transport.Send(pkt.ToBytes());
            return default;
        }

        private UsbPacket ReceiveRaw(int timeoutMs = 5000)
        {
            return UsbPacket.FromBytes(_transport.Receive(timeoutMs));
        }

        /// <summary>
        /// Receive a multi-packet data stream (DATA packets followed by DATA_END)
        /// </summary>
        private byte[] ReceiveDataStream(int timeoutMs = 10000)
        {
            using var ms = new MemoryStream();

            while (true)
            {
                var rsp = UsbPacket.FromBytes(_transport.Receive(timeoutMs));

                if (rsp.Cmd == UsbProto.RSP_DATA)
                {
                    if (rsp.PayloadLen > 0)
                        ms.Write(rsp.Data, 0, Math.Min((int)rsp.PayloadLen, 60));
                }
                else if (rsp.Cmd == UsbProto.RSP_DATA_END)
                {
                    break;
                }
                else if (rsp.Cmd == UsbProto.RSP_ERROR)
                {
                    throw new IOException($"Device error: 0x{rsp.Data[0]:X2}");
                }
                else
                {
                    throw new IOException($"Unexpected response: 0x{rsp.Cmd:X2}");
                }
            }

            return ms.ToArray();
        }

        // ====================== PUBLIC API ======================

        /// <summary>
        /// Ping the device, returns true if alive
        /// </summary>
        public bool Ping()
        {
            lock (_lock)
            {
                try
                {
                    var pkt = UsbPacket.Create(UsbProto.CMD_PING, NextSeq());
                    _transport.Send(pkt.ToBytes());
                    var rsp = UsbPacket.FromBytes(_transport.Receive(2000));
                    if (rsp.Cmd == UsbProto.RSP_PONG)
                    {
                        string ident = Encoding.ASCII.GetString(rsp.Data, 0, rsp.PayloadLen).TrimEnd('\0');
                        Console.WriteLine($"PONG: {ident}");
                        return true;
                    }
                    return false;
                }
                catch
                {
                    return false;
                }
            }
        }

        /// <summary>
        /// Read a file from the device
        /// </summary>
        public byte[] ReadFile(string remotePath, uint offset = 0, uint maxLen = 0)
        {
            lock (_lock)
            {
                byte seq = NextSeq();
                var pkt = UsbPacket.Create(UsbProto.CMD_READ_FILE, seq);

                // Pack: [offset:4][maxLen:4][path:ascii]
                BitConverter.GetBytes(offset).CopyTo(pkt.Data, 0);
                BitConverter.GetBytes(maxLen).CopyTo(pkt.Data, 4);

                byte[] pathBytes = Encoding.ASCII.GetBytes(remotePath + "\0");
                int pathLen = Math.Min(pathBytes.Length, UsbProto.PayloadSize - 8);
                Array.Copy(pathBytes, 0, pkt.Data, 8, pathLen);
                pkt.PayloadLen = (ushort)(8 + pathLen);

                _transport.Send(pkt.ToBytes());

                // Expect RSP_OK with file info
                var rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd == UsbProto.RSP_ERROR)
                    throw new FileNotFoundException($"Error 0x{rsp.Data[0]:X2}: {remotePath}");
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"Unexpected: 0x{rsp.Cmd:X2}");

                uint fileSize = BitConverter.ToUInt32(rsp.Data, 0);
                uint readLen = rsp.PayloadLen >= 8 ? BitConverter.ToUInt32(rsp.Data, 4) : fileSize;

                Console.WriteLine($"File size: {fileSize}, reading: {readLen}");

                // Now receive data stream
                return ReceiveDataStream(30000);
            }
        }

        /// <summary>
        /// Write a file to the device
        /// </summary>
        public void WriteFile(string remotePath, byte[] data)
        {
            lock (_lock)
            {
                if (data.Length > UsbProto.MaxTransferSize)
                    throw new ArgumentException($"File too large: {data.Length}");

                byte seq = NextSeq();

                // 1. Send initial command with path and size
                var pkt = UsbPacket.Create(UsbProto.CMD_WRITE_FILE, seq);
                BitConverter.GetBytes((uint)data.Length).CopyTo(pkt.Data, 0);

                byte[] pathBytes = Encoding.ASCII.GetBytes(remotePath + "\0");
                int pathLen = Math.Min(pathBytes.Length, UsbProto.PayloadSize - 4);
                Array.Copy(pathBytes, 0, pkt.Data, 4, pathLen);
                pkt.PayloadLen = (ushort)(4 + pathLen);

                _transport.Send(pkt.ToBytes());

                // Wait for ACK
                var rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd == UsbProto.RSP_ERROR)
                    throw new IOException($"Write setup failed: 0x{rsp.Data[0]:X2}");
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"Unexpected: 0x{rsp.Cmd:X2}");

                // 2. Stream data in 60-byte chunks
                int offset = 0;
                while (offset < data.Length)
                {
                    var dataPkt = UsbPacket.Create(UsbProto.CMD_WRITE_FILE, seq);
                    int chunk = Math.Min(UsbProto.PayloadSize, data.Length - offset);
                    Array.Copy(data, offset, dataPkt.Data, 0, chunk);
                    dataPkt.PayloadLen = (ushort)chunk;
                    _transport.Send(dataPkt.ToBytes());
                    offset += chunk;

                    // Small delay to avoid overwhelming the device
                    if (offset % (60 * 100) == 0)
                        Thread.Sleep(1);
                }

                // 3. Send end marker (empty payload)
                var endPkt = UsbPacket.Create(UsbProto.CMD_WRITE_FILE, seq);
                endPkt.PayloadLen = 0;
                _transport.Send(endPkt.ToBytes());

                // 4. Wait for final ACK
                rsp = UsbPacket.FromBytes(_transport.Receive(10000));
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"Write failed: 0x{rsp.Cmd:X2}");

                uint written = BitConverter.ToUInt32(rsp.Data, 0);
                Console.WriteLine($"Written {written} bytes to {remotePath}");
            }
        }

        /// <summary>
        /// List directory contents
        /// </summary>
        public DirEntryInfo[] ListDirectory(string pattern)
        {
            lock (_lock)
            {
                byte seq = NextSeq();
                var pkt = UsbPacket.Create(UsbProto.CMD_LIST_DIR, seq);

                byte[] patBytes = Encoding.ASCII.GetBytes(pattern + "\0");
                int len = Math.Min(patBytes.Length, UsbProto.PayloadSize);
                Array.Copy(patBytes, 0, pkt.Data, 0, len);
                pkt.PayloadLen = (ushort)len;

                _transport.Send(pkt.ToBytes());

                var entries = new System.Collections.Generic.List<DirEntryInfo>();

                while (true)
                {
                    var rsp = UsbPacket.FromBytes(_transport.Receive(5000));

                    if (rsp.Cmd == UsbProto.RSP_DIR_ENTRY)
                    {
                        var entry = new DirEntryInfo
                        {
                            Attributes = BitConverter.ToUInt32(rsp.Data, 0),
                            FileSize = BitConverter.ToUInt32(rsp.Data, 4),
                            Name = Encoding.ASCII.GetString(rsp.Data, 8,
                                Math.Max(0, rsp.PayloadLen - 9)).TrimEnd('\0')
                        };
                        entries.Add(entry);
                    }
                    else if (rsp.Cmd == UsbProto.RSP_DIR_END)
                    {
                        break;
                    }
                    else if (rsp.Cmd == UsbProto.RSP_ERROR)
                    {
                        throw new IOException($"ListDir error: 0x{rsp.Data[0]:X2}");
                    }
                    else
                    {
                        throw new IOException($"Unexpected: 0x{rsp.Cmd:X2}");
                    }
                }

                return entries.ToArray();
            }
        }

        /// <summary>
        /// Delete a file on the device
        /// </summary>
        public void DeleteFile(string remotePath)
        {
            lock (_lock)
            {
                var pkt = UsbPacket.Create(UsbProto.CMD_DELETE_FILE, NextSeq());
                byte[] pathBytes = Encoding.ASCII.GetBytes(remotePath + "\0");
                int len = Math.Min(pathBytes.Length, UsbProto.PayloadSize);
                Array.Copy(pathBytes, 0, pkt.Data, 0, len);
                pkt.PayloadLen = (ushort)len;

                var rsp = SendAndReceive(pkt);
                if (rsp.Cmd == UsbProto.RSP_ERROR)
                    throw new IOException($"Delete failed: 0x{rsp.Data[0]:X2}");
            }
        }

        /// <summary>
        /// Create a directory on the device
        /// </summary>
        public void MakeDirectory(string remotePath)
        {
            lock (_lock)
            {
                var pkt = UsbPacket.Create(UsbProto.CMD_MKDIR, NextSeq());
                byte[] pathBytes = Encoding.ASCII.GetBytes(remotePath + "\0");
                int len = Math.Min(pathBytes.Length, UsbProto.PayloadSize);
                Array.Copy(pathBytes, 0, pkt.Data, 0, len);
                pkt.PayloadLen = (ushort)len;

                var rsp = SendAndReceive(pkt);
                if (rsp.Cmd == UsbProto.RSP_ERROR)
                    throw new IOException($"Mkdir failed: 0x{rsp.Data[0]:X2}");
            }
        }

        /// <summary>
        /// Get file size/info
        /// </summary>
        public uint GetFileSize(string remotePath)
        {
            lock (_lock)
            {
                var pkt = UsbPacket.Create(UsbProto.CMD_FILE_INFO, NextSeq());
                byte[] pathBytes = Encoding.ASCII.GetBytes(remotePath + "\0");
                int len = Math.Min(pathBytes.Length, UsbProto.PayloadSize);
                Array.Copy(pathBytes, 0, pkt.Data, 0, len);
                pkt.PayloadLen = (ushort)len;

                var rsp = SendAndReceive(pkt);
                if (rsp.Cmd == UsbProto.RSP_ERROR)
                    throw new FileNotFoundException(remotePath);
                return BitConverter.ToUInt32(rsp.Data, 0);
            }
        }

        /// <summary>
        /// Read raw memory from device
        /// </summary>
        public byte[] ReadMemory(uint address, uint length)
        {
            lock (_lock)
            {
                byte seq = NextSeq();
                var pkt = UsbPacket.Create(UsbProto.CMD_READ_MEM, seq);
                BitConverter.GetBytes(address).CopyTo(pkt.Data, 0);
                BitConverter.GetBytes(length).CopyTo(pkt.Data, 4);
                pkt.PayloadLen = 8;

                _transport.Send(pkt.ToBytes());

                var rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"ReadMem failed: 0x{rsp.Cmd:X2}");

                return ReceiveDataStream(10000);
            }
        }

        /// <summary>
        /// Write raw memory on device
        /// </summary>
        public void WriteMemory(uint address, byte[] data)
        {
            lock (_lock)
            {
                byte seq = NextSeq();

                // Initial packet with address and length
                var pkt = UsbPacket.Create(UsbProto.CMD_WRITE_MEM, seq);
                BitConverter.GetBytes(address).CopyTo(pkt.Data, 0);
                BitConverter.GetBytes((uint)data.Length).CopyTo(pkt.Data, 4);

                // Include as much inline data as possible
                int inlineLen = Math.Min(data.Length, 52); // 60 - 8 header bytes
                if (inlineLen > 0)
                    Array.Copy(data, 0, pkt.Data, 8, inlineLen);
                pkt.PayloadLen = (ushort)(8 + inlineLen);

                _transport.Send(pkt.ToBytes());

                var rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"WriteMem setup failed: 0x{rsp.Cmd:X2}");

                // Send remaining data
                int offset = inlineLen;
                while (offset < data.Length)
                {
                    var dataPkt = UsbPacket.Create(UsbProto.CMD_WRITE_MEM, seq);
                    int chunk = Math.Min(UsbProto.PayloadSize, data.Length - offset);
                    Array.Copy(data, offset, dataPkt.Data, 0, chunk);
                    dataPkt.PayloadLen = (ushort)chunk;
                    _transport.Send(dataPkt.ToBytes());
                    offset += chunk;
                }

                // End marker
                var endPkt = UsbPacket.Create(UsbProto.CMD_WRITE_MEM, seq);
                endPkt.PayloadLen = 0;
                _transport.Send(endPkt.ToBytes());

                rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd != UsbProto.RSP_OK)
                    throw new IOException($"WriteMem failed: 0x{rsp.Cmd:X2}");
            }
        }

        /// <summary>
        /// Read the debug log ring buffer
        /// </summary>
        public string ReadLog()
        {
            lock (_lock)
            {
                byte seq = NextSeq();
                var pkt = UsbPacket.Create(UsbProto.CMD_LOG_READ, seq);
                _transport.Send(pkt.ToBytes());

                var rsp = UsbPacket.FromBytes(_transport.Receive(5000));
                if (rsp.Cmd != UsbProto.RSP_OK)
                    return $"[Error: 0x{rsp.Cmd:X2}]";

                uint totalLen = BitConverter.ToUInt32(rsp.Data, 0);
                if (totalLen == 0) return "";

                byte[] logData = ReceiveDataStream(10000);
                return Encoding.ASCII.GetString(logData);
            }
        }

        /// <summary>
        /// Inject a key event
        /// </summary>
        public void InjectKey(byte doomKey, bool isDown)
        {
            lock (_lock)
            {
                var pkt = UsbPacket.Create(UsbProto.CMD_KEY_INJECT, NextSeq());
                pkt.Data[0] = doomKey;
                pkt.Data[1] = (byte)(isDown ? 1 : 0);
                pkt.Data[2] = 0; // not mouse
                pkt.PayloadLen = 3;
                SendAndReceive(pkt);
            }
        }

        /// <summary>
        /// Inject a mouse move event
        /// </summary>
        public void InjectMouseMove(short dx, short dy)
        {
            lock (_lock)
            {
                var pkt = UsbPacket.Create(UsbProto.CMD_KEY_INJECT, NextSeq());
                pkt.Data[0] = 0;
                pkt.Data[1] = 0;
                pkt.Data[2] = 1; // is mouse
                BitConverter.GetBytes(dx).CopyTo(pkt.Data, 3);
                BitConverter.GetBytes(dy).CopyTo(pkt.Data, 5);
                pkt.PayloadLen = 7;
                SendAndReceive(pkt);
            }
        }

        /// <summary>
        /// Upload a local file to the device
        /// </summary>
        public void UploadFile(string localPath, string remotePath)
        {
            byte[] data = File.ReadAllBytes(localPath);
            Console.WriteLine($"Uploading {localPath} ({data.Length} bytes) -> {remotePath}");
            WriteFile(remotePath, data);
        }

        /// <summary>
        /// Download a file from device to local path
        /// </summary>
        public void DownloadFile(string remotePath, string localPath)
        {
            Console.WriteLine($"Downloading {remotePath} -> {localPath}");
            byte[] data = ReadFile(remotePath);
            File.WriteAllBytes(localPath, data);
            Console.WriteLine($"Downloaded {data.Length} bytes");
        }

        public void Dispose()
        {
            _transport?.Dispose();
        }
    }

    #endregion

    #region CLI / Debug Stub

    /// <summary>
    /// Simple pipe-based debug stub that simulates the device side
    /// for testing without hardware.
    /// </summary>
    public class DebugStub : IDisposable
    {
        private readonly PipeTransport _transport;
        private readonly string _rootDir;
        private bool _running;

        public DebugStub(string pipeName = "hpprime_debug", string simulatedRootDir = ".")
        {
            _rootDir = simulatedRootDir;
            _transport = new PipeTransport(pipeName, isServer: true);
        }

        public void Run()
        {
            _running = true;
            Console.WriteLine($"Debug stub running, simulated root: {_rootDir}");

            while (_running)
            {
                try
                {
                    byte[] raw = _transport.Receive(60000);
                    var pkt = UsbPacket.FromBytes(raw);
                    HandlePacket(pkt);
                }
                catch (TimeoutException)
                {
                    // Just continue
                }
                catch (IOException ex)
                {
                    Console.WriteLine($"Stub IO error: {ex.Message}");
                    _running = false;
                }
            }
        }

        private void HandlePacket(UsbPacket pkt)
        {
            Console.WriteLine($"Stub received cmd=0x{pkt.Cmd:X2} seq={pkt.Seq} len={pkt.PayloadLen}");

            switch (pkt.Cmd)
            {
                case UsbProto.CMD_PING:
                    {
                        var rsp = UsbPacket.Create(UsbProto.RSP_PONG, pkt.Seq);
                        byte[] ident = Encoding.ASCII.GetBytes("STUBDEV1");
                        Array.Copy(ident, rsp.Data, ident.Length);
                        rsp.PayloadLen = (ushort)ident.Length;
                        _transport.Send(rsp.ToBytes());
                    }
                    break;

                case UsbProto.CMD_READ_FILE:
                    StubReadFile(pkt);
                    break;

                case UsbProto.CMD_WRITE_FILE:
                    StubWriteFile(pkt);
                    break;

                case UsbProto.CMD_LIST_DIR:
                    StubListDir(pkt);
                    break;

                case UsbProto.CMD_FILE_INFO:
                    StubFileInfo(pkt);
                    break;

                case UsbProto.CMD_LOG_READ:
                    {
                        // Return a fake log
                        string log = "[STUB] Hello from debug stub!\n";
                        byte[] logData = Encoding.ASCII.GetBytes(log);

                        var okPkt = UsbPacket.Create(UsbProto.RSP_OK, pkt.Seq);
                        BitConverter.GetBytes((uint)logData.Length).CopyTo(okPkt.Data, 0);
                        okPkt.PayloadLen = 4;
                        _transport.Send(okPkt.ToBytes());

                        // Stream log data
                        int offset = 0;
                        while (offset < logData.Length)
                        {
                            var dataPkt = UsbPacket.Create(UsbProto.RSP_DATA, pkt.Seq);
                            int chunk = Math.Min(60, logData.Length - offset);
                            Array.Copy(logData, offset, dataPkt.Data, 0, chunk);
                            dataPkt.PayloadLen = (ushort)chunk;
                            _transport.Send(dataPkt.ToBytes());
                            offset += chunk;
                        }

                        var endPkt = UsbPacket.Create(UsbProto.RSP_DATA_END, pkt.Seq);
                        _transport.Send(endPkt.ToBytes());
                    }
                    break;

                default:
                    {
                        var rsp = UsbPacket.Create(UsbProto.RSP_ERROR, pkt.Seq);
                        rsp.Data[0] = UsbProto.ERR_UNKNOWN_CMD;
                        rsp.PayloadLen = 1;
                        _transport.Send(rsp.ToBytes());
                    }
                    break;
            }
        }

        private void StubReadFile(UsbPacket pkt)
        {
            uint offset = BitConverter.ToUInt32(pkt.Data, 0);
            uint maxLen = BitConverter.ToUInt32(pkt.Data, 4);
            string path = Encoding.ASCII.GetString(pkt.Data, 8, pkt.PayloadLen - 8).TrimEnd('\0');

            string localPath = Path.Combine(_rootDir, path.TrimStart('/'));
            Console.WriteLine($"Stub READ: {path} -> {localPath}");

            if (!File.Exists(localPath))
            {
                var err = UsbPacket.Create(UsbProto.RSP_ERROR, pkt.Seq);
                err.Data[0] = UsbProto.ERR_FILE_NOT_FOUND;
                err.PayloadLen = 1;
                _transport.Send(err.ToBytes());
                return;
            }

            byte[] fileData = File.ReadAllBytes(localPath);
            uint fileSize = (uint)fileData.Length;
            uint readLen = maxLen == 0 ? fileSize - offset : Math.Min(maxLen, fileSize - offset);

            // Send OK
            var ok = UsbPacket.Create(UsbProto.RSP_OK, pkt.Seq);
            BitConverter.GetBytes(fileSize).CopyTo(ok.Data, 0);
            BitConverter.GetBytes(readLen).CopyTo(ok.Data, 4);
            ok.PayloadLen = 8;
            _transport.Send(ok.ToBytes());

            // Stream data
            int pos = (int)offset;
            int remaining = (int)readLen;
            while (remaining > 0)
            {
                var dataPkt = UsbPacket.Create(UsbProto.RSP_DATA, pkt.Seq);
                int chunk = Math.Min(60, remaining);
                Array.Copy(fileData, pos, dataPkt.Data, 0, chunk);
                dataPkt.PayloadLen = (ushort)chunk;
                _transport.Send(dataPkt.ToBytes());
                pos += chunk;
                remaining -= chunk;
            }

            var end = UsbPacket.Create(UsbProto.RSP_DATA_END, pkt.Seq);
            _transport.Send(end.ToBytes());
        }

        private void StubWriteFile(UsbPacket pkt)
        {
            uint totalSize = BitConverter.ToUInt32(pkt.Data, 0);
            string path = Encoding.ASCII.GetString(pkt.Data, 4, pkt.PayloadLen - 4).TrimEnd('\0');
            string localPath = Path.Combine(_rootDir, path.TrimStart('/'));

            Console.WriteLine($"Stub WRITE: {path} ({totalSize} bytes) -> {localPath}");

            // Ensure directory exists
            string dir = Path.GetDirectoryName(localPath);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);

            // ACK
            var ok = UsbPacket.Create(UsbProto.RSP_OK, pkt.Seq);
            ok.Data[0] = UsbProto.ERR_NONE;
            ok.PayloadLen = 1;
            _transport.Send(ok.ToBytes());

            // Receive data
            using var ms = new MemoryStream();
            while (ms.Length < totalSize)
            {
                var dataPkt = UsbPacket.FromBytes(_transport.Receive(10000));
                if (dataPkt.Cmd == UsbProto.CMD_WRITE_FILE && dataPkt.PayloadLen == 0)
                    break;
                if (dataPkt.PayloadLen > 0)
                    ms.Write(dataPkt.Data, 0, dataPkt.PayloadLen);
            }

            File.WriteAllBytes(localPath, ms.ToArray());

            // Final ACK
            var done = UsbPacket.Create(UsbProto.RSP_OK, pkt.Seq);
            BitConverter.GetBytes((uint)ms.Length).CopyTo(done.Data, 0);
            done.PayloadLen = 4;
            _transport.Send(done.ToBytes());
        }

        private void StubListDir(UsbPacket pkt)
        {
            string pattern = Encoding.ASCII.GetString(pkt.Data, 0, pkt.PayloadLen).TrimEnd('\0');
            string localDir = Path.GetDirectoryName(Path.Combine(_rootDir, pattern.TrimStart('/')));
            string searchPattern = Path.GetFileName(pattern);

            Console.WriteLine($"Stub LIST: {pattern}");

            if (string.IsNullOrEmpty(searchPattern)) searchPattern = "*";
            if (string.IsNullOrEmpty(localDir)) localDir = _rootDir;

            if (Directory.Exists(localDir))
            {
                foreach (var entry in Directory.GetFileSystemEntries(localDir, searchPattern))
                {
                    var info = new FileInfo(entry);
                    bool isDir = (info.Attributes & FileAttributes.Directory) != 0;

                    var rsp = UsbPacket.Create(UsbProto.RSP_DIR_ENTRY, pkt.Seq);
                    BitConverter.GetBytes(isDir ? 0x10u : 0u).CopyTo(rsp.Data, 0);
                    BitConverter.GetBytes(isDir ? 0u : (uint)info.Length).CopyTo(rsp.Data, 4);

                    string name = Path.GetFileName(entry);
                    byte[] nameBytes = Encoding.ASCII.GetBytes(name + "\0");
                    int nameLen = Math.Min(nameBytes.Length, 51);
                    Array.Copy(nameBytes, 0, rsp.Data, 8, nameLen);
                    rsp.PayloadLen = (ushort)(8 + nameLen);
                    _transport.Send(rsp.ToBytes());
                }
            }

            var end = UsbPacket.Create(UsbProto.RSP_DIR_END, pkt.Seq);
            _transport.Send(end.ToBytes());
        }

        private void StubFileInfo(UsbPacket pkt)
        {
            string path = Encoding.ASCII.GetString(pkt.Data, 0, pkt.PayloadLen).TrimEnd('\0');
            string localPath = Path.Combine(_rootDir, path.TrimStart('/'));

            if (!File.Exists(localPath))
            {
                var err = UsbPacket.Create(UsbProto.RSP_ERROR, pkt.Seq);
                err.Data[0] = UsbProto.ERR_FILE_NOT_FOUND;
                err.PayloadLen = 1;
                _transport.Send(err.ToBytes());
                return;
            }

            var ok = UsbPacket.Create(UsbProto.RSP_OK, pkt.Seq);
            BitConverter.GetBytes((uint)new FileInfo(localPath).Length).CopyTo(ok.Data, 0);
            ok.PayloadLen = 4;
            _transport.Send(ok.ToBytes());
        }

        public void Dispose()
        {
            _running = false;
            _transport?.Dispose();
        }
    }

    /// <summary>
    /// Command-line interface
    /// </summary>
    public class Program
    {
        static void Main(string[] args)
        {
            if (args.Length == 0)
            {
                PrintUsage();
                return;
            }

            string mode = args[0].ToLower();

            if (mode == "stub")
            {
                // Run as debug stub (simulated device)
                string rootDir = args.Length > 1 ? args[1] : ".";
                using var stub = new DebugStub(simulatedRootDir: rootDir);
                stub.Run();
                return;
            }

            // Determine transport
            IUsbTransport transport;
            bool usePipe = args.Any(a => a == "--pipe");

            if (usePipe)
            {
                transport = new PipeTransport(isServer: false);
            }
            else
            {
                string devicePath = args.FirstOrDefault(a => a.StartsWith("--dev="))?.Substring(6);
                transport = new WinUsbTransport(devicePath);
            }

            using var client = new HpPrimeClient(transport);

            // Remove transport flags from args for command parsing
            args = args.Where(a => a != "--pipe" && !a.StartsWith("--dev=")).ToArray();

            try
            {
                switch (args[0].ToLower())
                {
                    case "ping":
                        bool alive = client.Ping();
                        Console.WriteLine(alive ? "Device is alive!" : "No response.");
                        break;

                    case "ls":
                        if (args.Length < 2) { Console.WriteLine("Usage: ls <pattern>"); break; }
                        var entries = client.ListDirectory(args[1]);
                        foreach (var e in entries)
                            Console.WriteLine(e);
                        Console.WriteLine($"\n{entries.Length} entries");
                        break;

                    case "get":
                        if (args.Length < 3) { Console.WriteLine("Usage: get <remote> <local>"); break; }
                        client.DownloadFile(args[1], args[2]);
                        break;

                    case "put":
                        if (args.Length < 3) { Console.WriteLine("Usage: put <local> <remote>"); break; }
                        client.UploadFile(args[1], args[2]);
                        break;

                    case "rm":
                        if (args.Length < 2) { Console.WriteLine("Usage: rm <remote>"); break; }
                        client.DeleteFile(args[1]);
                        Console.WriteLine("Deleted.");
                        break;

                    case "mkdir":
                        if (args.Length < 2) { Console.WriteLine("Usage: mkdir <remote>"); break; }
                        client.MakeDirectory(args[1]);
                        Console.WriteLine("Created.");
                        break;

                    case "info":
                        if (args.Length < 2) { Console.WriteLine("Usage: info <remote>"); break; }
                        uint size = client.GetFileSize(args[1]);
                        Console.WriteLine($"Size: {size} bytes");
                        break;

                    case "log":
                        string log = client.ReadLog();
                        Console.WriteLine("=== Device Log ===");
                        Console.Write(log);
                        Console.WriteLine("=== End Log ===");
                        break;

                    case "readmem":
                        if (args.Length < 3) { Console.WriteLine("Usage: readmem <addr> <len> [outfile]"); break; }
                        uint addr = Convert.ToUInt32(args[1], 16);
                        uint len = Convert.ToUInt32(args[2], 16);
                        byte[] mem = client.ReadMemory(addr, len);
                        if (args.Length > 3)
                        {
                            File.WriteAllBytes(args[3], mem);
                            Console.WriteLine($"Saved {mem.Length} bytes to {args[3]}");
                        }
                        else
                        {
                            // Hex dump
                            for (int i = 0; i < mem.Length; i += 16)
                            {
                                Console.Write($"{addr + i:X8}: ");
                                for (int j = 0; j < 16 && i + j < mem.Length; j++)
                                    Console.Write($"{mem[i + j]:X2} ");
                                Console.WriteLine();
                            }
                        }
                        break;

                    case "writemem":
                        if (args.Length < 3) { Console.WriteLine("Usage: writemem <addr> <file>"); break; }
                        uint waddr = Convert.ToUInt32(args[1], 16);
                        byte[] wdata = File.ReadAllBytes(args[2]);
                        client.WriteMemory(waddr, wdata);
                        Console.WriteLine($"Wrote {wdata.Length} bytes to 0x{waddr:X8}");
                        break;

                    case "monitor":
                        // Continuously poll and display log
                        Console.WriteLine("Monitoring log (Ctrl+C to stop)...");
                        while (true)
                        {
                            string logText = client.ReadLog();
                            if (!string.IsNullOrEmpty(logText))
                            {
                                Console.Write(logText);
                            }
                            Thread.Sleep(500);
                        }

                    default:
                        PrintUsage();
                        break;
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Error: {ex.Message}");
                Environment.Exit(1);
            }
        }

        static void PrintUsage()
        {
            Console.WriteLine(@"
HP Prime DOOM USB Tool
======================
Usage: hpprime <command> [options]

Transport options:
  --pipe          Use named pipe transport (for testing with stub)
  --dev=<path>    Specify USB device path

Commands:
  ping                          Test connectivity
  ls <pattern>                  List directory (e.g., ls ""/path/*"")
  get <remote> <local>          Download file
  put <local> <remote>          Upload file  
  rm <remote>                   Delete file
  mkdir <remote>                Create directory
  info <remote>                 Get file info
  log                           Read debug log
  readmem <addr> <len> [file]   Read device memory (hex addresses)
  writemem <addr> <file>        Write file to device memory
  monitor                       Continuously display debug log

Debug stub:
  stub [rootdir]                Run as simulated device (pipe server)

Examples:
  hpprime --pipe stub ./testroot     # Terminal 1: start stub
  hpprime --pipe ping                # Terminal 2: test connection
  hpprime --pipe ls ""/*""              
  hpprime --pipe put doom1.wad /doom1.wad
  hpprime --pipe get /doom1.wad local_copy.wad
");
        }
    }

    #endregion
}