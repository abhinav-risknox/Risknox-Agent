package org.opensearch.dataprepper.plugins.windowsevent;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for XmlParserUtils — validates XML parsing against real Windows Event
 * XML samples from EvtRender(EvtRenderEventXml).
 */
class XmlParserUtilsTest {

    // ── Sysmon Event 1: Process Create ──────────────────────────────────────

    private static final String SYSMON_EVENT_1_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Sysmon' Guid='{5770385f-c22a-43e0-bf4c-06f5698ffbd9}'/>" +
        "    <EventID>1</EventID>" +
        "    <Version>5</Version>" +
        "    <Level>4</Level>" +
        "    <Task>1</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8000000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T12:00:00.123456Z'/>" +
        "    <EventRecordID>12345</EventRecordID>" +
        "    <Correlation/>" +
        "    <Execution ProcessID='1840' ThreadID='4560'/>" +
        "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
        "    <Computer>WORKSTATION-01</Computer>" +
        "    <Security UserID='S-1-5-18'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='RuleName'>technique_id=T1059</Data>" +
        "    <Data Name='UtcTime'>2025-07-06 12:00:00.123</Data>" +
        "    <Data Name='ProcessGuid'>{abc-def-123}</Data>" +
        "    <Data Name='ProcessId'>4567</Data>" +
        "    <Data Name='Image'>C:\\Windows\\System32\\cmd.exe</Data>" +
        "    <Data Name='FileVersion'>10.0.19041.1</Data>" +
        "    <Data Name='Description'>Windows Command Processor</Data>" +
        "    <Data Name='Product'>Microsoft® Windows® Operating System</Data>" +
        "    <Data Name='Company'>Microsoft Corporation</Data>" +
        "    <Data Name='OriginalFileName'>Cmd.Exe</Data>" +
        "    <Data Name='CommandLine'>cmd.exe /c whoami</Data>" +
        "    <Data Name='CurrentDirectory'>C:\\Users\\admin\\</Data>" +
        "    <Data Name='User'>WORKSTATION-01\\admin</Data>" +
        "    <Data Name='LogonGuid'>{abc-def-456}</Data>" +
        "    <Data Name='LogonId'>0x12345</Data>" +
        "    <Data Name='TerminalSessionId'>1</Data>" +
        "    <Data Name='IntegrityLevel'>High</Data>" +
        "    <Data Name='Hashes'>SHA256=abc123def456</Data>" +
        "    <Data Name='ParentProcessGuid'>{abc-def-789}</Data>" +
        "    <Data Name='ParentProcessId'>1234</Data>" +
        "    <Data Name='ParentImage'>C:\\Windows\\explorer.exe</Data>" +
        "    <Data Name='ParentCommandLine'>C:\\Windows\\explorer.exe</Data>" +
        "    <Data Name='ParentUser'>WORKSTATION-01\\admin</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Sysmon Event 1 — system fields extracted correctly")
    void testSysmonEvent1SystemFields() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_1_XML);

        assertEquals("1", result.systemFields.get("EventID"));
        assertEquals("5", result.systemFields.get("Version"));
        assertEquals("4", result.systemFields.get("Level"));
        assertEquals("1", result.systemFields.get("Task"));
        assertEquals("0", result.systemFields.get("Opcode"));
        assertEquals("0x8000000000000000", result.systemFields.get("Keywords"));
        assertEquals("12345", result.systemFields.get("EventRecordID"));
        assertEquals("Microsoft-Windows-Sysmon/Operational", result.systemFields.get("Channel"));
        assertEquals("WORKSTATION-01", result.systemFields.get("Computer"));

        // Attribute-based system fields
        assertEquals("Microsoft-Windows-Sysmon", result.systemFields.get("Provider_Name"));
        assertEquals("{5770385f-c22a-43e0-bf4c-06f5698ffbd9}", result.systemFields.get("Provider_Guid"));
        assertEquals("2025-07-06T12:00:00.123456Z", result.systemFields.get("TimeCreated_SystemTime"));
        assertEquals("1840", result.systemFields.get("Execution_ProcessID"));
        assertEquals("4560", result.systemFields.get("Execution_ThreadID"));
        assertEquals("S-1-5-18", result.systemFields.get("Security_UserID"));
    }

    @Test
    @DisplayName("Sysmon Event 1 — eventData fields preserve PascalCase names")
    void testSysmonEvent1EventData() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_1_XML);

        assertEquals("C:\\Windows\\System32\\cmd.exe", result.eventData.get("Image"));
        assertEquals("cmd.exe /c whoami", result.eventData.get("CommandLine"));
        assertEquals("WORKSTATION-01\\admin", result.eventData.get("User"));
        assertEquals("High", result.eventData.get("IntegrityLevel"));
        assertEquals("C:\\Users\\admin\\", result.eventData.get("CurrentDirectory"));
        assertEquals("{abc-def-123}", result.eventData.get("ProcessGuid"));
        assertEquals("4567", result.eventData.get("ProcessId"));
        assertEquals("SHA256=abc123def456", result.eventData.get("Hashes"));
        assertEquals("C:\\Windows\\explorer.exe", result.eventData.get("ParentImage"));
        assertEquals("C:\\Windows\\explorer.exe", result.eventData.get("ParentCommandLine"));
        assertEquals("1234", result.eventData.get("ParentProcessId"));
        assertEquals("technique_id=T1059", result.eventData.get("RuleName"));
        assertEquals("Cmd.Exe", result.eventData.get("OriginalFileName"));
        assertEquals("Microsoft Corporation", result.eventData.get("Company"));

        // Verify NO system fields leaked into eventData
        assertNull(result.eventData.get("EventID"));
        assertNull(result.eventData.get("Channel"));
        assertNull(result.eventData.get("Computer"));
    }

    // ── Sysmon Event 3: Network Connection ──────────────────────────────────

    private static final String SYSMON_EVENT_3_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Sysmon' Guid='{5770385f-c22a-43e0-bf4c-06f5698ffbd9}'/>" +
        "    <EventID>3</EventID>" +
        "    <Version>5</Version>" +
        "    <Level>4</Level>" +
        "    <Task>3</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8000000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T12:01:00.000Z'/>" +
        "    <EventRecordID>12346</EventRecordID>" +
        "    <Correlation/>" +
        "    <Execution ProcessID='1840' ThreadID='4560'/>" +
        "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
        "    <Computer>WORKSTATION-01</Computer>" +
        "    <Security UserID='S-1-5-18'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='Image'>C:\\Windows\\System32\\svchost.exe</Data>" +
        "    <Data Name='User'>NT AUTHORITY\\SYSTEM</Data>" +
        "    <Data Name='Protocol'>tcp</Data>" +
        "    <Data Name='Initiated'>true</Data>" +
        "    <Data Name='SourceIp'>192.168.1.100</Data>" +
        "    <Data Name='SourcePort'>49152</Data>" +
        "    <Data Name='DestinationIp'>10.0.0.1</Data>" +
        "    <Data Name='DestinationPort'>443</Data>" +
        "    <Data Name='DestinationHostname'>api.example.com</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Sysmon Event 3 — network fields extracted correctly")
    void testSysmonEvent3Network() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_3_XML);

        assertEquals("3", result.systemFields.get("EventID"));
        assertEquals("192.168.1.100", result.eventData.get("SourceIp"));
        assertEquals("49152", result.eventData.get("SourcePort"));
        assertEquals("10.0.0.1", result.eventData.get("DestinationIp"));
        assertEquals("443", result.eventData.get("DestinationPort"));
        assertEquals("api.example.com", result.eventData.get("DestinationHostname"));
        assertEquals("tcp", result.eventData.get("Protocol"));
        assertEquals("true", result.eventData.get("Initiated"));
    }

    // ── Security Event 4624: Logon ──────────────────────────────────────────

    private static final String SECURITY_4624_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Security-Auditing' Guid='{54849625-5478-4994-a5ba-3e3b0328c30d}'/>" +
        "    <EventID>4624</EventID>" +
        "    <Version>2</Version>" +
        "    <Level>0</Level>" +
        "    <Task>12544</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8020000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T10:30:00.000Z'/>" +
        "    <EventRecordID>99999</EventRecordID>" +
        "    <Correlation ActivityID='{abcd-1234-5678}'/>" +
        "    <Execution ProcessID='600' ThreadID='700'/>" +
        "    <Channel>Security</Channel>" +
        "    <Computer>DC-01.corp.local</Computer>" +
        "    <Security/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='SubjectUserSid'>S-1-5-18</Data>" +
        "    <Data Name='SubjectUserName'>DC-01$</Data>" +
        "    <Data Name='SubjectDomainName'>CORP</Data>" +
        "    <Data Name='SubjectLogonId'>0x3e7</Data>" +
        "    <Data Name='TargetUserSid'>S-1-5-21-123-456-789-1001</Data>" +
        "    <Data Name='TargetUserName'>jdoe</Data>" +
        "    <Data Name='TargetDomainName'>CORP</Data>" +
        "    <Data Name='LogonType'>10</Data>" +
        "    <Data Name='LogonProcessName'>User32</Data>" +
        "    <Data Name='AuthenticationPackageName'>Negotiate</Data>" +
        "    <Data Name='WorkstationName'>LAPTOP-01</Data>" +
        "    <Data Name='IpAddress'>192.168.1.50</Data>" +
        "    <Data Name='IpPort'>54321</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Security Event 4624 — authentication fields extracted")
    void testSecurityEvent4624() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SECURITY_4624_XML);

        assertEquals("4624", result.systemFields.get("EventID"));
        assertEquals("Security", result.systemFields.get("Channel"));
        assertEquals("DC-01.corp.local", result.systemFields.get("Computer"));
        assertEquals("{abcd-1234-5678}", result.systemFields.get("Correlation_ActivityID"));

        assertEquals("jdoe", result.eventData.get("TargetUserName"));
        assertEquals("CORP", result.eventData.get("TargetDomainName"));
        assertEquals("10", result.eventData.get("LogonType"));
        assertEquals("Negotiate", result.eventData.get("AuthenticationPackageName"));
        assertEquals("192.168.1.50", result.eventData.get("IpAddress"));
        assertEquals("54321", result.eventData.get("IpPort"));
        assertEquals("S-1-5-18", result.eventData.get("SubjectUserSid"));
    }

    // ── Sysmon Event 22: DNS Query ──────────────────────────────────────────

    private static final String SYSMON_EVENT_22_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Sysmon' Guid='{5770385f-c22a-43e0-bf4c-06f5698ffbd9}'/>" +
        "    <EventID>22</EventID>" +
        "    <Level>4</Level>" +
        "    <Task>22</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8000000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T14:00:00.000Z'/>" +
        "    <EventRecordID>55555</EventRecordID>" +
        "    <Execution ProcessID='1840' ThreadID='4560'/>" +
        "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
        "    <Computer>WORKSTATION-01</Computer>" +
        "    <Security UserID='S-1-5-18'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='Image'>C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe</Data>" +
        "    <Data Name='QueryName'>malware-c2.evil.com</Data>" +
        "    <Data Name='QueryResults'>type: 1 ::ffff:198.51.100.1;</Data>" +
        "    <Data Name='QueryStatus'>0</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Sysmon Event 22 — DNS query fields extracted")
    void testSysmonEvent22Dns() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_22_XML);

        assertEquals("22", result.systemFields.get("EventID"));
        assertEquals("malware-c2.evil.com", result.eventData.get("QueryName"));
        assertEquals("type: 1 ::ffff:198.51.100.1;", result.eventData.get("QueryResults"));
        assertEquals("0", result.eventData.get("QueryStatus"));
    }

    // ── PowerShell Event 4104: Script Block ─────────────────────────────────

    private static final String POWERSHELL_4104_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-PowerShell' Guid='{a0c1853b-5c40-4b15-8766-3cf1c58f985a}'/>" +
        "    <EventID>4104</EventID>" +
        "    <Level>5</Level>" +
        "    <Task>2</Task>" +
        "    <Opcode>15</Opcode>" +
        "    <Keywords>0x0</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T15:00:00.000Z'/>" +
        "    <EventRecordID>77777</EventRecordID>" +
        "    <Execution ProcessID='5000' ThreadID='6000'/>" +
        "    <Channel>Microsoft-Windows-PowerShell/Operational</Channel>" +
        "    <Computer>WORKSTATION-01</Computer>" +
        "    <Security UserID='S-1-5-21-123-456-789-1001'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='MessageNumber'>1</Data>" +
        "    <Data Name='MessageTotal'>1</Data>" +
        "    <Data Name='ScriptBlockText'>Invoke-Mimikatz -DumpCreds</Data>" +
        "    <Data Name='ScriptBlockId'>abc-123-script</Data>" +
        "    <Data Name='Path'>C:\\temp\\evil.ps1</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("PowerShell Event 4104 — script block text extracted")
    void testPowerShell4104() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(POWERSHELL_4104_XML);

        assertEquals("4104", result.systemFields.get("EventID"));
        assertEquals("Microsoft-Windows-PowerShell/Operational", result.systemFields.get("Channel"));
        assertEquals("Invoke-Mimikatz -DumpCreds", result.eventData.get("ScriptBlockText"));
        assertEquals("C:\\temp\\evil.ps1", result.eventData.get("Path"));
    }

    // ── Sysmon Event 12/13: Registry ────────────────────────────────────────

    private static final String SYSMON_EVENT_13_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Sysmon' Guid='{5770385f-c22a-43e0-bf4c-06f5698ffbd9}'/>" +
        "    <EventID>13</EventID>" +
        "    <Level>4</Level>" +
        "    <Task>13</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8000000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T16:00:00.000Z'/>" +
        "    <EventRecordID>88888</EventRecordID>" +
        "    <Execution ProcessID='1840' ThreadID='4560'/>" +
        "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
        "    <Computer>WORKSTATION-01</Computer>" +
        "    <Security UserID='S-1-5-18'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='EventType'>SetValue</Data>" +
        "    <Data Name='Image'>C:\\Windows\\regedit.exe</Data>" +
        "    <Data Name='TargetObject'>HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run\\Malware</Data>" +
        "    <Data Name='Details'>C:\\temp\\malware.exe</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Sysmon Event 13 — registry fields extracted")
    void testSysmonEvent13Registry() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_13_XML);

        assertEquals("13", result.systemFields.get("EventID"));
        assertEquals("SetValue", result.eventData.get("EventType"));
        assertEquals("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run\\Malware",
                     result.eventData.get("TargetObject"));
        assertEquals("C:\\temp\\malware.exe", result.eventData.get("Details"));
    }

    // ── Edge cases ──────────────────────────────────────────────────────────

    @Test
    @DisplayName("Null XML returns empty ParsedEvent")
    void testNullXml() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(null);
        assertTrue(result.systemFields.isEmpty());
        assertTrue(result.eventData.isEmpty());
    }

    @Test
    @DisplayName("Empty string returns empty ParsedEvent")
    void testEmptyXml() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse("");
        assertTrue(result.systemFields.isEmpty());
        assertTrue(result.eventData.isEmpty());
    }

    @Test
    @DisplayName("Malformed XML returns partial results")
    void testMalformedXml() {
        String malformed =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='Test-Provider'/>" +
            "    <EventID>999</EventID>" +
            "    <Channel>TestChannel</Channel>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data Name='GoodField'>GoodValue</Data>" +
            "    <Data Name='Broken"; // Truncated XML

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(malformed);

        // Should have extracted what it could before the error
        assertEquals("Test-Provider", result.systemFields.get("Provider_Name"));
        assertEquals("999", result.systemFields.get("EventID"));
        assertEquals("GoodValue", result.eventData.get("GoodField"));
    }

    @Test
    @DisplayName("Data tags without Name attribute get numbered keys")
    void testUnnamedDataTags() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='TestProvider'/>" +
            "    <EventID>100</EventID>" +
            "    <Channel>Application</Channel>" +
            "    <Computer>SERVER-01</Computer>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data>First unnamed value</Data>" +
            "    <Data>Second unnamed value</Data>" +
            "    <Data Name='NamedField'>Named value</Data>" +
            "    <Data>Third unnamed value</Data>" +
            "  </EventData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("First unnamed value", result.eventData.get("Data_0"));
        assertEquals("Second unnamed value", result.eventData.get("Data_1"));
        assertEquals("Named value", result.eventData.get("NamedField"));
        assertEquals("Third unnamed value", result.eventData.get("Data_2"));
    }

    @Test
    @DisplayName("UserData block parsed like EventData")
    void testUserDataBlock() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='Microsoft-Windows-GroupPolicy'/>" +
            "    <EventID>4001</EventID>" +
            "    <Channel>Microsoft-Windows-GroupPolicy/Operational</Channel>" +
            "    <Computer>DC-01</Computer>" +
            "  </System>" +
            "  <UserData>" +
            "    <Data Name='PolicyElaspedTimeInSeconds'>5</Data>" +
            "    <Data Name='ErrorCode'>0</Data>" +
            "    <Data Name='IsMachine'>true</Data>" +
            "  </UserData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("4001", result.systemFields.get("EventID"));
        assertEquals("5", result.eventData.get("PolicyElaspedTimeInSeconds"));
        assertEquals("0", result.eventData.get("ErrorCode"));
        assertEquals("true", result.eventData.get("IsMachine"));
    }

    @Test
    @DisplayName("Empty EventData fields stored as empty strings")
    void testEmptyEventDataFields() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='TestProvider'/>" +
            "    <EventID>1</EventID>" +
            "    <Channel>Test</Channel>" +
            "    <Computer>PC-01</Computer>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data Name='NonEmpty'>value</Data>" +
            "    <Data Name='EmptyField'></Data>" +
            "    <Data Name='AlsoEmpty'/>" +
            "  </EventData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("value", result.eventData.get("NonEmpty"));
        assertEquals("", result.eventData.get("EmptyField"));
        // Self-closing <Data Name='AlsoEmpty'/> should also be captured
        assertTrue(result.eventData.containsKey("AlsoEmpty"));
    }

    // ── Security Event 4688: Process Creation (non-Sysmon) ──────────────────

    private static final String SECURITY_4688_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Microsoft-Windows-Security-Auditing'/>" +
        "    <EventID>4688</EventID>" +
        "    <Level>0</Level>" +
        "    <Task>13312</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8020000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T17:00:00.000Z'/>" +
        "    <EventRecordID>44444</EventRecordID>" +
        "    <Execution ProcessID='4' ThreadID='100'/>" +
        "    <Channel>Security</Channel>" +
        "    <Computer>SERVER-02</Computer>" +
        "    <Security/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='SubjectUserSid'>S-1-5-18</Data>" +
        "    <Data Name='SubjectUserName'>SERVER-02$</Data>" +
        "    <Data Name='SubjectDomainName'>CORP</Data>" +
        "    <Data Name='SubjectLogonId'>0x3e7</Data>" +
        "    <Data Name='NewProcessId'>0x1a2b</Data>" +
        "    <Data Name='NewProcessName'>C:\\Windows\\System32\\whoami.exe</Data>" +
        "    <Data Name='CommandLine'>whoami /all</Data>" +
        "    <Data Name='ProcessId'>0x0c3d</Data>" +
        "    <Data Name='ParentProcessName'>C:\\Windows\\System32\\cmd.exe</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("Security Event 4688 — process creation fields extracted")
    void testSecurityEvent4688() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SECURITY_4688_XML);

        assertEquals("4688", result.systemFields.get("EventID"));
        assertEquals("Security", result.systemFields.get("Channel"));
        assertEquals("C:\\Windows\\System32\\whoami.exe", result.eventData.get("NewProcessName"));
        assertEquals("whoami /all", result.eventData.get("CommandLine"));
        assertEquals("C:\\Windows\\System32\\cmd.exe", result.eventData.get("ParentProcessName"));
    }

    // ── Service Install Event 7045 ──────────────────────────────────────────

    private static final String SYSTEM_7045_XML =
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
        "  <System>" +
        "    <Provider Name='Service Control Manager' Guid='{555908d1-a6d7-4695-8e1e-26931d2012f4}'/>" +
        "    <EventID>7045</EventID>" +
        "    <Level>4</Level>" +
        "    <Task>0</Task>" +
        "    <Opcode>0</Opcode>" +
        "    <Keywords>0x8080000000000000</Keywords>" +
        "    <TimeCreated SystemTime='2025-07-06T18:00:00.000Z'/>" +
        "    <EventRecordID>66666</EventRecordID>" +
        "    <Execution ProcessID='600' ThreadID='700'/>" +
        "    <Channel>System</Channel>" +
        "    <Computer>SERVER-03</Computer>" +
        "    <Security UserID='S-1-5-18'/>" +
        "  </System>" +
        "  <EventData>" +
        "    <Data Name='ServiceName'>Malicious Service</Data>" +
        "    <Data Name='ImagePath'>C:\\temp\\backdoor.exe</Data>" +
        "    <Data Name='ServiceType'>user mode service</Data>" +
        "    <Data Name='StartType'>auto start</Data>" +
        "    <Data Name='AccountName'>LocalSystem</Data>" +
        "  </EventData>" +
        "</Event>";

    @Test
    @DisplayName("System Event 7045 — service install fields extracted")
    void testSystemEvent7045() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSTEM_7045_XML);

        assertEquals("7045", result.systemFields.get("EventID"));
        assertEquals("System", result.systemFields.get("Channel"));
        assertEquals("Malicious Service", result.eventData.get("ServiceName"));
        assertEquals("C:\\temp\\backdoor.exe", result.eventData.get("ImagePath"));
        assertEquals("auto start", result.eventData.get("StartType"));
        assertEquals("LocalSystem", result.eventData.get("AccountName"));
    }

    // ── Sysmon Event 7: Image Loaded ────────────────────────────────────────

    @Test
    @DisplayName("Sysmon Event 7 — image load fields extracted")
    void testSysmonEvent7ImageLoad() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='Microsoft-Windows-Sysmon'/>" +
            "    <EventID>7</EventID>" +
            "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
            "    <Computer>WS-01</Computer>" +
            "    <TimeCreated SystemTime='2025-07-06T19:00:00.000Z'/>" +
            "    <Execution ProcessID='100' ThreadID='200'/>" +
            "    <Security UserID='S-1-5-18'/>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data Name='Image'>C:\\Windows\\System32\\svchost.exe</Data>" +
            "    <Data Name='ImageLoaded'>C:\\Windows\\System32\\ntdll.dll</Data>" +
            "    <Data Name='Signed'>true</Data>" +
            "    <Data Name='Signature'>Microsoft Windows</Data>" +
            "    <Data Name='SignatureStatus'>Valid</Data>" +
            "    <Data Name='Hashes'>SHA256=deadbeef</Data>" +
            "  </EventData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("7", result.systemFields.get("EventID"));
        assertEquals("C:\\Windows\\System32\\ntdll.dll", result.eventData.get("ImageLoaded"));
        assertEquals("true", result.eventData.get("Signed"));
        assertEquals("Microsoft Windows", result.eventData.get("Signature"));
        assertEquals("Valid", result.eventData.get("SignatureStatus"));
    }

    // ── Sysmon Event 10: Process Access ─────────────────────────────────────

    @Test
    @DisplayName("Sysmon Event 10 — process access fields extracted")
    void testSysmonEvent10ProcessAccess() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='Microsoft-Windows-Sysmon'/>" +
            "    <EventID>10</EventID>" +
            "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
            "    <Computer>WS-01</Computer>" +
            "    <TimeCreated SystemTime='2025-07-06T20:00:00.000Z'/>" +
            "    <Execution ProcessID='100' ThreadID='200'/>" +
            "    <Security UserID='S-1-5-18'/>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data Name='SourceImage'>C:\\temp\\mimikatz.exe</Data>" +
            "    <Data Name='TargetImage'>C:\\Windows\\System32\\lsass.exe</Data>" +
            "    <Data Name='GrantedAccess'>0x1010</Data>" +
            "    <Data Name='CallTrace'>C:\\Windows\\SYSTEM32\\ntdll.dll+9d4c4</Data>" +
            "  </EventData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("10", result.systemFields.get("EventID"));
        assertEquals("C:\\temp\\mimikatz.exe", result.eventData.get("SourceImage"));
        assertEquals("C:\\Windows\\System32\\lsass.exe", result.eventData.get("TargetImage"));
        assertEquals("0x1010", result.eventData.get("GrantedAccess"));
    }

    // ── Sysmon Event 11: File Created ───────────────────────────────────────

    @Test
    @DisplayName("Sysmon Event 11 — file creation fields extracted")
    void testSysmonEvent11FileCreate() {
        String xml =
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>" +
            "  <System>" +
            "    <Provider Name='Microsoft-Windows-Sysmon'/>" +
            "    <EventID>11</EventID>" +
            "    <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
            "    <Computer>WS-01</Computer>" +
            "    <TimeCreated SystemTime='2025-07-06T21:00:00.000Z'/>" +
            "    <Execution ProcessID='100' ThreadID='200'/>" +
            "    <Security UserID='S-1-5-18'/>" +
            "  </System>" +
            "  <EventData>" +
            "    <Data Name='Image'>C:\\Windows\\explorer.exe</Data>" +
            "    <Data Name='TargetFilename'>C:\\Users\\admin\\Downloads\\payload.exe</Data>" +
            "    <Data Name='CreationUtcTime'>2025-07-06 21:00:00.000</Data>" +
            "  </EventData>" +
            "</Event>";

        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(xml);

        assertEquals("11", result.systemFields.get("EventID"));
        assertEquals("C:\\Users\\admin\\Downloads\\payload.exe", result.eventData.get("TargetFilename"));
    }

    // ── Verify total field count for Sysmon Event 1 ─────────────────────────

    @Test
    @DisplayName("Sysmon Event 1 — correct total number of eventData fields")
    void testSysmonEvent1FieldCount() {
        XmlParserUtils.ParsedEvent result = XmlParserUtils.parse(SYSMON_EVENT_1_XML);

        // Event 1 XML has 23 <Data> elements
        assertEquals(23, result.eventData.size(),
            "Expected 23 EventData fields from Sysmon Event 1");
    }

    // ── Legacy compatibility ────────────────────────────────────────────────

    @Test
    @DisplayName("Legacy parseEventData() returns flat combined map")
    @SuppressWarnings("deprecation")
    void testLegacyParseEventData() {
        Map<String, Object> flat = XmlParserUtils.parseEventData(SYSMON_EVENT_1_XML);

        // Should contain both system and event data fields
        assertEquals("1", flat.get("EventID"));
        assertEquals("Microsoft-Windows-Sysmon", flat.get("Provider_Name"));
        assertEquals("C:\\Windows\\System32\\cmd.exe", flat.get("Image"));
        assertEquals("cmd.exe /c whoami", flat.get("CommandLine"));
    }
}
