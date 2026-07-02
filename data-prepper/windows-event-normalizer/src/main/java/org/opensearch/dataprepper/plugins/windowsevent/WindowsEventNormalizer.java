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
                Map<String, Object> normalized = normalizeToWinlog(rawParsed);
                
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

    private Map<String, Object> normalizeToWinlog(Map<String, Object> raw) {
        Map<String, Object> result = new HashMap<>();

        // @timestamp for OpenSearch time-based indexing
        String timestamp = safeGetString(raw, "TimeCreated_SystemTime");
        if (timestamp.isEmpty()) timestamp = Instant.now().toString();
        result.put("@timestamp", timestamp);

        // Build winlog structure — the only structure OSSA's Windows log type needs.
        // OSSA auto-maps: EventID→winlog.event_id, Channel→winlog.channel, etc.
        Map<String, Object> winlogObj = new HashMap<>();
        winlogObj.put("event_id", safeGetInt(raw, "EventID"));
        winlogObj.put("provider_name", safeGetString(raw, "Provider_Name"));
        winlogObj.put("channel", safeGetString(raw, "Channel"));
        winlogObj.put("computer_name", safeGetString(raw, "Computer"));
        winlogObj.put("record_id", safeGetInt(raw, "EventRecordID"));

        // Build event_data with ONLY EventData fields (not System fields).
        // All values coerced to strings — OSSA rules expect keyword type.
        // OSSA auto-maps each raw_field (e.g. CommandLine) to winlog.event_data.CommandLine
        Map<String, Object> eventData = new HashMap<>();
        for (Map.Entry<String, Object> entry : raw.entrySet()) {
            if (isSystemField(entry.getKey())) continue;
            Object val = entry.getValue();
            eventData.put(entry.getKey(), val != null ? String.valueOf(val) : null);
        }
        winlogObj.put("event_data", eventData);

        result.put("winlog", winlogObj);

        return result;
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
}
