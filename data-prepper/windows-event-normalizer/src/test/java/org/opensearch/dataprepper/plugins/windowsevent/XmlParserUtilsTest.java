package org.opensearch.dataprepper.plugins.windowsevent;

import org.junit.jupiter.api.Test;
import org.opensearch.dataprepper.model.event.Event;
import org.opensearch.dataprepper.model.event.JacksonEvent;
import org.opensearch.dataprepper.model.record.Record;

import java.util.Collection;
import java.util.Collections;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

public class XmlParserUtilsTest {

    private static final String SYSMON_EVENT1_XML =
            "<Event xmlns=\"http://schemas.microsoft.com/win/2004/08/events/event\">" +
            "<System>" +
            "  <Provider Name=\"Microsoft-Windows-Sysmon\" Guid=\"{5770385F-C22A-43E0-BF4C-06F5698FFBD9}\"/>" +
            "  <EventID>1</EventID>" +
            "  <Version>5</Version>" +
            "  <Level>4</Level>" +
            "  <Task>1</Task>" +
            "  <Opcode>0</Opcode>" +
            "  <Keywords>0x8000000000000000</Keywords>" +
            "  <TimeCreated SystemTime=\"2026-06-30T10:00:00.000000Z\"/>" +
            "  <EventRecordID>12345</EventRecordID>" +
            "  <Correlation/>" +
            "  <Execution ProcessID=\"4\" ThreadID=\"8\"/>" +
            "  <Channel>Microsoft-Windows-Sysmon/Operational</Channel>" +
            "  <Computer>TEST-PC</Computer>" +
            "  <Security UserID=\"S-1-5-18\"/>" +
            "</System>" +
            "<EventData>" +
            "  <Data Name=\"RuleName\">-</Data>" +
            "  <Data Name=\"UtcTime\">2026-06-30 10:00:00.000</Data>" +
            "  <Data Name=\"ProcessGuid\">{00000000-0000-0000-0000-000000000000}</Data>" +
            "  <Data Name=\"ProcessId\">5000</Data>" +
            "  <Data Name=\"Image\">C:\\Windows\\System32\\cmd.exe</Data>" +
            "  <Data Name=\"FileVersion\">10.0.19041.746</Data>" +
            "  <Data Name=\"Description\">Windows Command Processor</Data>" +
            "  <Data Name=\"Product\">Microsoft Windows Operating System</Data>" +
            "  <Data Name=\"Company\">Microsoft Corporation</Data>" +
            "  <Data Name=\"OriginalFileName\">Cmd.Exe</Data>" +
            "  <Data Name=\"CommandLine\">\"C:\\Windows\\System32\\cmd.exe\" /c echo test</Data>" +
            "  <Data Name=\"CurrentDirectory\">C:\\</Data>" +
            "  <Data Name=\"User\">TEST-PC\\TestUser</Data>" +
            "  <Data Name=\"LogonGuid\">{00000000-0000-0000-0000-000000000000}</Data>" +
            "  <Data Name=\"LogonId\">0x3e7</Data>" +
            "  <Data Name=\"TerminalSessionId\">1</Data>" +
            "  <Data Name=\"IntegrityLevel\">High</Data>" +
            "  <Data Name=\"Hashes\">SHA1=123,MD5=456,SHA256=789</Data>" +
            "  <Data Name=\"ParentProcessGuid\">{00000000-0000-0000-0000-000000000000}</Data>" +
            "  <Data Name=\"ParentProcessId\">4000</Data>" +
            "  <Data Name=\"ParentImage\">C:\\Windows\\explorer.exe</Data>" +
            "  <Data Name=\"ParentCommandLine\">C:\\Windows\\explorer.exe</Data>" +
            "</EventData>" +
            "</Event>";

    @Test
    public void testSysmonEvent1Extraction() {
        Map<String, Object> eventData = XmlParserUtils.parseEventData(SYSMON_EVENT1_XML);
        assertNotNull(eventData);

        // All values should be strings (tryParseNumber no longer converts to Long)
        assertEquals("Microsoft-Windows-Sysmon", eventData.get("Provider_Name"));
        assertEquals("1", eventData.get("EventID"));
        assertEquals("2026-06-30T10:00:00.000000Z", eventData.get("TimeCreated_SystemTime"));
        assertEquals("5000", eventData.get("ProcessId"));
        assertEquals("C:\\Windows\\System32\\cmd.exe", eventData.get("Image"));
        assertEquals("SHA1=123,MD5=456,SHA256=789", eventData.get("Hashes"));
        assertEquals("4000", eventData.get("ParentProcessId"));
    }

    @Test
    public void testEcsNormalization() {
        String xml = "<Event xmlns=\"http://schemas.microsoft.com/win/2004/08/events/event\">" +
                "<System><Provider Name=\"Microsoft-Windows-Sysmon\"/><EventID>1</EventID></System>" +
                "<EventData>" +
                "<Data Name=\"ProcessId\">5000</Data>" +
                "<Data Name=\"Image\">C:\\Windows\\System32\\cmd.exe</Data>" +
                "<Data Name=\"CommandLine\">cmd.exe /c test</Data>" +
                "<Data Name=\"User\">DOMAIN\\User</Data>" +
                "</EventData></Event>";

        WindowsEventNormalizerConfig config = new WindowsEventNormalizerConfig();
        WindowsEventNormalizer normalizer = new WindowsEventNormalizer(config);

        Event event = JacksonEvent.builder()
                .withEventType("event")
                .withData(Map.of("data", xml))
                .build();

        Record<Event> record = new Record<>(event);
        Collection<Record<Event>> results = normalizer.execute(Collections.singletonList(record));

        Event processedEvent = results.iterator().next().getData();

        // ECS process fields still work (safeGetInt handles string→int)
        Map<String, Object> process = processedEvent.get("process", Map.class);
        assertNotNull(process);
        assertEquals(5000, process.get("pid"));
        assertEquals("C:\\Windows\\System32\\cmd.exe", process.get("executable"));
        assertEquals("cmd.exe", process.get("name"));
        assertEquals("cmd.exe /c test", process.get("command_line"));

        Map<String, Object> user = processedEvent.get("user", Map.class);
        assertNotNull(user);
        assertEquals("DOMAIN\\User", user.get("name"));
    }

    @SuppressWarnings("unchecked")
    @Test
    public void testWinlogStructure() {
        WindowsEventNormalizerConfig config = new WindowsEventNormalizerConfig();
        WindowsEventNormalizer normalizer = new WindowsEventNormalizer(config);

        Event event = JacksonEvent.builder()
                .withEventType("event")
                .withData(Map.of("data", SYSMON_EVENT1_XML))
                .build();

        Record<Event> record = new Record<>(event);
        Collection<Record<Event>> results = normalizer.execute(Collections.singletonList(record));
        Event processedEvent = results.iterator().next().getData();

        // Verify winlog top-level fields
        Map<String, Object> winlog = processedEvent.get("winlog", Map.class);
        assertNotNull(winlog, "winlog object must exist");
        assertEquals(1, winlog.get("event_id"), "winlog.event_id should be integer");
        assertEquals("Microsoft-Windows-Sysmon", winlog.get("provider_name"));
        assertEquals("Microsoft-Windows-Sysmon/Operational", winlog.get("channel"));
        assertEquals("TEST-PC", winlog.get("computer_name"));

        // Verify computerObject
        Map<String, Object> computerObj = (Map<String, Object>) winlog.get("computerObject");
        assertNotNull(computerObj);
        assertEquals("TEST-PC", computerObj.get("name"));

        // Verify event_data contains EventData fields as strings
        Map<String, Object> eventData = (Map<String, Object>) winlog.get("event_data");
        assertNotNull(eventData);
        assertEquals("C:\\Windows\\System32\\cmd.exe", eventData.get("Image"));
        assertEquals("\"C:\\Windows\\System32\\cmd.exe\" /c echo test", eventData.get("CommandLine"));
        assertEquals("C:\\Windows\\explorer.exe", eventData.get("ParentImage"));
        assertEquals("5000", eventData.get("ProcessId"), "event_data values should be strings");

        // System fields must NOT be in event_data
        assertNull(eventData.get("EventID"), "EventID is a System field, not EventData");
        assertNull(eventData.get("Provider_Name"), "Provider_Name is a System field");
        assertNull(eventData.get("Channel"), "Channel is a System field");
        assertNull(eventData.get("Computer"), "Computer is a System field");
        assertNull(eventData.get("TimeCreated_SystemTime"), "TimeCreated is a System field");
    }
}
