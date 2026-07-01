package org.opensearch.dataprepper.plugins.windowsevent;

import org.opensearch.dataprepper.model.annotations.DataPrepperPlugin;
import org.opensearch.dataprepper.model.annotations.DataPrepperPluginConstructor;
import org.opensearch.dataprepper.model.event.Event;
import org.opensearch.dataprepper.model.record.Record;
import org.opensearch.dataprepper.model.processor.Processor;

import java.time.Instant;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

@DataPrepperPlugin(name = "windows_event_normalizer", pluginType = Processor.class, pluginConfigurationType = WindowsEventNormalizerConfig.class)
public class WindowsEventNormalizer implements Processor<Record<Event>, Record<Event>> {

    private final WindowsEventNormalizerConfig config;

    // System-level XML fields extracted by XmlParserUtils from <System> block.
    // These must NOT appear in winlog.event_data (which should only contain EventData fields).
    private static final Set<String> SYSTEM_FIELDS = Set.of(
        "EventID", "Level", "Task", "Opcode", "Keywords", "Channel", "Computer",
        "EventRecordID", "Version",
        "Provider_Name", "Provider_Guid",
        "TimeCreated_SystemTime",
        "Execution_ProcessID", "Execution_ThreadID",
        "Security_UserID",
        "Correlation_ActivityID"
    );

    private static boolean isSystemField(String key) {
        return SYSTEM_FIELDS.contains(key);
    }

    @DataPrepperPluginConstructor
    public WindowsEventNormalizer(final WindowsEventNormalizerConfig config) {
        this.config = config;
    }

    @Override
    public Collection<Record<Event>> execute(Collection<Record<Event>> records) {
        return records.stream().map(record -> {
            Event event = record.getData();
            String xmlData = event.get(config.getSourceField(), String.class);
            
            if (xmlData != null && !xmlData.isEmpty()) {
                Map<String, Object> rawParsed = XmlParserUtils.parseEventData(xmlData);
                Map<String, Object> normalized = normalizeToEcs(rawParsed);
                
                // Put all normalized fields into the event root
                for (Map.Entry<String, Object> entry : normalized.entrySet()) {
                    event.put(entry.getKey(), entry.getValue());
                }
                
                if (config.isDropRawXml()) {
                    event.delete(config.getSourceField());
                }
            }
            return record;
        }).collect(Collectors.toList());
    }

    @Override
    public void prepareForShutdown() {}

    @Override
    public boolean isReadyForShutdown() { return true; }

    @Override
    public void shutdown() {}

    private Map<String, Object> normalizeToEcs(Map<String, Object> raw) {
        Map<String, Object> ecs = new HashMap<>();
        
        // 1. Base Event Fields
        int eventId = safeGetInt(raw, "EventID");
        String provider = safeGetString(raw, "Provider_Name");
        String timestamp = safeGetString(raw, "TimeCreated_SystemTime");
        if (timestamp.isEmpty()) timestamp = Instant.now().toString();

        Map<String, Object> eventObj = new HashMap<>();
        eventObj.put("id", eventId);
        eventObj.put("provider", provider);
        eventObj.put("kind", "event");
        eventObj.put("channel", safeGetString(raw, "Channel"));
        eventObj.put("level", safeGetString(raw, "Level"));
        eventObj.put("keywords", safeGetString(raw, "Keywords"));
        eventObj.put("created", timestamp);
        eventObj.put("record_id", safeGetInt(raw, "EventRecordID"));
        eventObj.put("user_id", safeGetString(raw, "Security_UserID"));
        ecs.put("event", eventObj);
        ecs.put("@timestamp", timestamp);

        Map<String, Object> hostObj = new HashMap<>();
        hostObj.put("hostname", safeGetString(raw, "Computer"));
        ecs.put("host", hostObj);

        // Build winlog structure matching OSSA's expected schema for Sigma detection
        Map<String, Object> winlogObj = new HashMap<>();
        winlogObj.put("event_id", eventId);
        winlogObj.put("provider_name", provider);
        winlogObj.put("channel", safeGetString(raw, "Channel"));
        winlogObj.put("computer_name", safeGetString(raw, "Computer"));
        winlogObj.put("record_id", safeGetInt(raw, "EventRecordID"));

        // Build event_data with ONLY EventData fields (not System fields).
        // All values coerced to strings — OSSA rules expect keyword type.
        Map<String, Object> eventData = new HashMap<>();
        for (Map.Entry<String, Object> entry : raw.entrySet()) {
            if (isSystemField(entry.getKey())) continue;
            Object val = entry.getValue();
            eventData.put(entry.getKey(), val != null ? String.valueOf(val) : null);
        }
        winlogObj.put("event_data", eventData);

        // computerObject.name — some OSSA rules check this path
        Map<String, Object> computerObj = new HashMap<>();
        computerObj.put("name", safeGetString(raw, "Computer"));
        winlogObj.put("computerObject", computerObj);

        ecs.put("winlog", winlogObj);

        // 2. Specific Event Type Normalizations
        boolean isSysmon = provider.contains("Sysmon");
        
        if ((isSysmon && eventId == 1) || (!isSysmon && eventId == 4688)) {
            ecs.put("process", extractProcessEcs(raw, eventId, isSysmon));
            ecs.put("user", extractUserEcs(raw, isSysmon ? "User" : "TargetUserName", isSysmon ? "" : "TargetDomainName"));
        }
        else if ((isSysmon && eventId == 3) || (!isSysmon && eventId == 5140)) {
            ecs.put("source", extractNetworkEcs(raw, isSysmon ? "SourceIp" : "IpAddress", isSysmon ? "SourcePort" : "IpPort"));
            ecs.put("destination", extractNetworkEcs(raw, isSysmon ? "DestinationIp" : "", isSysmon ? "DestinationPort" : ""));
            ecs.put("network", Map.of("protocol", safeGetString(raw, "Protocol")));
            if (eventId == 3) ecs.put("process", extractProcessEcs(raw, eventId, isSysmon));
        }
        else if ((isSysmon && eventId == 11) || (!isSysmon && eventId == 4663)) {
            ecs.put("file", extractFileEcs(raw, isSysmon ? "TargetFilename" : "ObjectName"));
            if (eventId == 11) ecs.put("process", extractProcessEcs(raw, eventId, isSysmon));
        }
        else if (isSysmon && (eventId == 12 || eventId == 13 || eventId == 14) || (!isSysmon && eventId == 4657)) {
            ecs.put("registry", extractRegistryEcs(raw, isSysmon ? "TargetObject" : "ObjectName", isSysmon ? "Details" : "NewValueData"));
            if (isSysmon) ecs.put("process", extractProcessEcs(raw, eventId, isSysmon));
        }
        else if (!isSysmon && (eventId == 4624 || eventId == 4625)) {
            ecs.put("user", extractUserEcs(raw, "TargetUserName", "TargetDomainName"));
            ecs.put("source", extractNetworkEcs(raw, "IpAddress", "IpPort"));
        }

        return ecs;
    }

    private Map<String, Object> extractProcessEcs(Map<String, Object> raw, int eventId, boolean isSysmon) {
        Map<String, Object> process = new HashMap<>();
        Map<String, Object> parent = new HashMap<>();

        if (isSysmon) {
            process.put("pid", safeGetInt(raw, "ProcessId"));
            process.put("executable", safeGetString(raw, "Image"));
            process.put("command_line", safeGetString(raw, "CommandLine"));
            process.put("entity_id", safeGetString(raw, "ProcessGuid"));
            
            parent.put("pid", safeGetInt(raw, "ParentProcessId"));
            parent.put("executable", safeGetString(raw, "ParentImage"));
            parent.put("command_line", safeGetString(raw, "ParentCommandLine"));
            parent.put("entity_id", safeGetString(raw, "ParentProcessGuid"));
            
            String hashes = safeGetString(raw, "Hashes");
            if (!hashes.isEmpty()) {
                Map<String, String> hashObj = new HashMap<>();
                for (String pair : hashes.split(",")) {
                    String[] kv = pair.split("=");
                    if (kv.length == 2) hashObj.put(kv[0].toLowerCase(), kv[1]);
                }
                process.put("hash", hashObj);
            }
        } else {
            if (eventId == 4688) {
                process.put("pid", parseHexInt(safeGetString(raw, "NewProcessId")));
                process.put("executable", safeGetString(raw, "NewProcessName"));
                process.put("command_line", safeGetString(raw, "CommandLine"));
                parent.put("pid", parseHexInt(safeGetString(raw, "ProcessId")));
                parent.put("executable", safeGetString(raw, "ParentProcessName"));
            } else {
                process.put("pid", parseHexInt(safeGetString(raw, "ProcessId")));
                process.put("executable", safeGetString(raw, "ProcessName"));
            }
        }

        if (!parent.isEmpty()) {
            process.put("parent", parent);
        }
        
        // Extract process name from executable
        String executable = safeGetString(process, "executable");
        if (!executable.isEmpty()) {
            int lastSlash = executable.lastIndexOf('\\');
            process.put("name", lastSlash != -1 ? executable.substring(lastSlash + 1) : executable);
        }

        return process;
    }

    private Map<String, Object> extractNetworkEcs(Map<String, Object> raw, String ipField, String portField) {
        Map<String, Object> net = new HashMap<>();
        String ip = safeGetString(raw, ipField);
        if (!ip.isEmpty() && !ip.equals("-")) net.put("ip", ip);
        
        Integer port = safeGetInt(raw, portField);
        if (port != null && port != 0) net.put("port", port);
        
        return net;
    }

    private Map<String, Object> extractFileEcs(Map<String, Object> raw, String pathField) {
        Map<String, Object> file = new HashMap<>();
        String path = safeGetString(raw, pathField);
        if (!path.isEmpty()) {
            file.put("path", path);
            int lastSlash = path.lastIndexOf('\\');
            if (lastSlash != -1) {
                String name = path.substring(lastSlash + 1);
                file.put("name", name);
                int lastDot = name.lastIndexOf('.');
                if (lastDot != -1) {
                    file.put("extension", name.substring(lastDot + 1));
                }
            } else {
                file.put("name", path);
            }
        }
        return file;
    }

    private Map<String, Object> extractRegistryEcs(Map<String, Object> raw, String pathField, String valueField) {
        Map<String, Object> reg = new HashMap<>();
        String path = safeGetString(raw, pathField);
        if (!path.isEmpty()) {
            reg.put("path", path);
            int lastSlash = path.lastIndexOf('\\');
            if (lastSlash != -1) {
                reg.put("key", path.substring(0, lastSlash));
                reg.put("value", path.substring(lastSlash + 1));
            } else {
                reg.put("key", path);
            }
        }
        String details = safeGetString(raw, valueField);
        if (!details.isEmpty()) {
            reg.put("data", Map.of("strings", details));
        }
        return reg;
    }

    private Map<String, Object> extractUserEcs(Map<String, Object> raw, String nameField, String domainField) {
        Map<String, Object> user = new HashMap<>();
        String name = safeGetString(raw, nameField);
        if (!name.isEmpty() && !name.equals("-")) {
            user.put("name", name);
        }
        String domain = safeGetString(raw, domainField);
        if (!domain.isEmpty() && !domain.equals("-")) {
            user.put("domain", domain);
        }
        return user;
    }

    private String safeGetString(Map<String, Object> map, String key) {
        Object val = map.get(key);
        return val == null ? "" : val.toString();
    }

    private Integer safeGetInt(Map<String, Object> map, String key) {
        Object val = map.get(key);
        if (val instanceof Number) return ((Number)val).intValue();
        if (val instanceof String) {
            try { return Integer.parseInt((String)val); } catch (Exception e) {}
        }
        return 0; // Default to 0 for int instead of null to prevent NPE in autoboxing, or handle nulls
    }

    private Integer parseHexInt(String hexStr) {
        if (hexStr == null || hexStr.isEmpty() || hexStr.equals("-")) return 0;
        try {
            if (hexStr.startsWith("0x") || hexStr.startsWith("0X")) {
                return Integer.parseInt(hexStr.substring(2), 16);
            }
            return Integer.parseInt(hexStr);
        } catch (Exception e) {
            return 0;
        }
    }
}
