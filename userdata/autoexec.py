import xbmc
import os

# Autoexec script to improve buffering, network and performance on Android/Nvidia Shield
# This script writes an advancedsettings.xml with high-performance caching and network parameters.

ADVANCED_SETTINGS = """<advancedsettings>
    <cache>
        <buffermode>1</buffermode> <!-- 1 = buffer all internet streams -->
        <memorysize>268435456</memorysize> <!-- 256MB buffer size; adjust as needed -->
        <readfactor>8</readfactor> <!-- read ahead 8x playback rate -->
    </cache>
    <network>
        <curlclienttimeout>20</curlclienttimeout>
        <curllowspeedtime>0</curllowspeedtime>
        <nfschunksize>1048576</nfschunksize> <!-- 1MB NFS chunk size -->
        <smbchunksize>1048576</smbchunksize> <!-- 1MB SMB chunk size -->
        <noofbuffers>4</noofbuffers>
    </network>
</advancedsettings>"""

def write_advanced_settings():
    profile_path = xbmc.translatePath('special://profile/')
    xml_path = os.path.join(profile_path, 'advancedsettings.xml')
    try:
        with open(xml_path, 'w') as f:
            f.write(ADVANCED_SETTINGS)
        xbmc.log('[autoexec] Written advancedsettings.xml for Shield tuning', xbmc.LOGNOTICE)
    except Exception as e:
        xbmc.log('[autoexec] Failed to write advancedsettings.xml: {}'.format(e), xbmc.LOGERROR)

# Execute on startup
write_advanced_settings()
