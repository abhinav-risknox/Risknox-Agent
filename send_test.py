import socket, json
xml = '''<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event">
  <System>
    <Provider Name="Microsoft-Windows-Sysmon" Guid="{5770385F-C22A-43E0-BF4C-06F5698FFBD9}"/>
    <EventID>1</EventID>
    <Version>5</Version>
    <Level>4</Level>
    <Task>1</Task>
    <Opcode>0</Opcode>
    <Keywords>0x8000000000000000</Keywords>
    <TimeCreated SystemTime="2024-07-16T10:15:30.123456Z"/>
    <EventRecordID>99999</EventRecordID>
    <Correlation/>
    <Execution ProcessID="3000" ThreadID="3004"/>
    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>
    <Computer>WORKSTATION-02</Computer>
    <Security UserID="S-1-5-18"/>
  </System>
  <EventData>
    <Data Name="RuleName">-</Data>
    <Data Name="UtcTime">2024-07-16 10:15:30.123</Data>
    <Data Name="ProcessGuid">{A1B2C3D4-E5F6-7890-1234-567890ABCDEF}</Data>
    <Data Name="ProcessId">4444</Data>
    <Data Name="Image">C:\Windows\System32\rundll32.exe</Data>
    <Data Name="FileVersion">10.0.19041.1 (WinBuild.160101.0800)</Data>
    <Data Name="Description">Windows host process (Rundll32)</Data>
    <Data Name="Product">Microsoft Windows Operating System</Data>
    <Data Name="Company">Microsoft Corporation</Data>
    <Data Name="OriginalFileName">RUNDLL32.EXE</Data>
    <Data Name="CommandLine">rundll32.exe user32.dll,LockWorkStation</Data>
    <Data Name="CurrentDirectory">C:\Windows\System32\</Data>
    <Data Name="User">NT AUTHORITY\SYSTEM</Data>
    <Data Name="LogonGuid">{A1B2C3D4-E5F6-7890-1234-567890ABCD00}</Data>
    <Data Name="LogonId">0x3e7</Data>
    <Data Name="TerminalSessionId">0</Data>
    <Data Name="IntegrityLevel">System</Data>
    <Data Name="Hashes">SHA1=2A3B4C5D6E7F8091A2B3C4D5E6F708192A3B4C5D,MD5=1A2B3C4D5E6F708192A3B4C5D6E7F809</Data>
    <Data Name="ParentProcessGuid">{A1B2C3D4-E5F6-7890-1234-567890ABCD11}</Data>
    <Data Name="ParentProcessId">1000</Data>
    <Data Name="ParentImage">C:\Windows\System32\svchost.exe</Data>
    <Data Name="ParentCommandLine">C:\Windows\System32\svchost.exe -k netsvcs</Data>
    <Data Name="ParentUser">NT AUTHORITY\SYSTEM</Data>
  </EventData>
</Event>'''
msg = json.dumps({'source_type': 'winevent', 'data': xml}) + '\n'
s = socket.socket()
s.connect(('localhost', 5170))
s.sendall(msg.encode())
s.close()
