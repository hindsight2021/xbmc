import xbmc
import os

# Autoexec script to improve buffering, network and performance on Android/Nvidia Shield
# This script writes an advancedsettings.xml with high-performance caching and network parameters.
# It dynamically adjusts buffer sizes and chunk sizes based on available memory and whether
# the device is on wired ethernet.

def is_ethernet_connected():
    """
    Returns True if the eth0 interface is reported as connected. On Android-based systems like
    the Nvidia Shield, the carrier file in /sys/class/net/eth0 may exist. If it doesn't exist,
    we assume Ethernet is disconnected.
    """
    try:
        with open('/sys/class/net/eth0/carrier', 'r') as f:
            return f.read().strip() == '1'
    except Exception:
        return False

def get_total_memory():
    """
    Return total physical memory in bytes by parsing /proc/meminfo.
    """
    try:
        with open('/proc/meminfo') as f:
            for line in f:
                if line.startswith('MemTotal:'):
                    parts = line.split()
                    # value is in kB
                    return int(parts[1]) * 1024
    except Exception:
        pass
    return 0

def write_advanced_settings():
    # Determine dynamic values
    wired = is_ethernet_connected()
    total_mem = get_total_memory()

    # Calculate buffer size: use 20% of total memory, max 512MB; default 256MB if unknown
    if total_mem > 0:
        dynamic_buffer = int(total_mem * 0.2)
        max_buffer = 512 * 1024 * 1024
        dynamic_buffer = min(dynamic_buffer, max_buffer)
    else:
        dynamic_buffer = 256 * 1024 * 1024
    # Round down to nearest 4KB to satisfy Kodi requirements
    dynamic_buffer = (dynamic_buffer // 4096) * 4096

    # Set chunk sizes based on connection type
    if wired:
        nfs_chunk = 1 * 1024 * 1024
        smb_chunk = 1 * 1024 * 1024
        read_factor = 8
    else:
        nfs_chunk = 256 * 1024
        smb_chunk = 256 * 1024
        read_factor = 6

    # Build advanced settings XML
    xml = f"""<advancedsettings>
    <cache>
        <buffermode>1</buffermode>
        <memorysize>{dynamic_buffer}</memorysize>
        <readfactor>{read_factor}</readfactor>
    </cache>
    <network>
        <curlclienttimeout>20</curlclienttimeout>
        <curllowspeedtime>10</curllowspeedtime>
        <nfschunksize>{nfs_chunk}</nfschunksize>
        <smbchunksize>{smb_chunk}</smbchunksize>
        <noofbuffers>4</noofbuffers>
    </network>
</advancedsettings>"""

    profile_path = xbmc.translatePath('special://profile/')
    xml_path = os.path.join(profile_path, 'advancedsettings.xml')
    try:
        with open(xml_path, 'w') as f:
            f.write(xml)
        xbmc.log(f'[autoexec] Wrote dynamic advancedsettings.xml (buffer={dynamic_buffer}, nfs_chunk={nfs_chunk}, smb_chunk={smb_chunk}, readfactor={read_factor}, wired={wired})', xbmc.LOGNOTICE)
    except Exception as e:
        xbmc.log('[autoexec] Failed to write advancedsettings.xml: {}'.format(e), xbmc.LOGERROR)

write_advanced_settings()
