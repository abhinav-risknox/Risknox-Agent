package org.opensearch.dataprepper.plugins.windowsevent;

import org.opensearch.dataprepper.model.annotations.DataPrepperPlugin;
import org.opensearch.dataprepper.model.annotations.DataPrepperPluginConstructor;
import org.opensearch.dataprepper.model.event.Event;
import org.opensearch.dataprepper.model.record.Record;
import org.opensearch.dataprepper.model.processor.Processor;

import java.time.Instant;
import java.util.Collection;
import java.util.stream.Collectors;

/**
 * Data Prepper processor that parses raw Windows Event XML (in the
 * {@code data} field) and sets the extracted fields at the root of the event.
 */
@DataPrepperPlugin(name = "windows_xml_parser", pluginType = Processor.class, pluginConfigurationType = WindowsXmlParserConfig.class)
public class WindowsXmlParser implements Processor<Record<Event>, Record<Event>> {

    private final WindowsXmlParserConfig config;

    @DataPrepperPluginConstructor
    public WindowsXmlParser(final WindowsXmlParserConfig config) {
        this.config = config;
    }

    @Override
    public Collection<Record<Event>> execute(Collection<Record<Event>> records) {
        return records.stream().map(record -> {
            Event event = record.getData();

            // Skip non-winevent events
            String sourceType = event.get("source_type", String.class);
            if (sourceType != null && !"winevent".equals(sourceType)) {
                return record;
            }

            String xmlData = event.get(config.getSourceField(), String.class);

            if (xmlData != null && !xmlData.isEmpty() && xmlData.trim().startsWith("<")) {
                processXmlEvent(event, xmlData);
            }

            return record;
        }).collect(Collectors.toList());
    }

    /**
     * Parse the XML and write flat fields into the event.
     * Field names are kept as-is from the XML (PascalCase) to match OSSA
     * {@code windows_logtype.json} raw_field names for auto-mapping.
     */
    private void processXmlEvent(Event event, String xmlData) {
        XmlParserUtils.ParsedEvent parsed = XmlParserUtils.parse(xmlData);

        // 1. Write all system metadata to the root
        parsed.systemFields.forEach(event::put);

        // 2. Write all EventData fields to the root
        parsed.eventData.forEach(event::put);

        // 3. Rename fields to match OSSA raw_field names exactly
        //    XML <Computer> tag → OSSA expects "ComputerName"
        String computer = parsed.systemFields.get("Computer");
        if (computer != null) {
            event.put("ComputerName", computer);
            event.delete("Computer");
        }

        // 4. Set @timestamp
        String timestamp = parsed.systemFields.get("TimeCreated_SystemTime");
        if (timestamp == null || timestamp.isEmpty()) {
            timestamp = event.get("timestamp", String.class);
        }
        if (timestamp == null || timestamp.isEmpty()) {
            timestamp = Instant.now().toString();
        }
        event.put("@timestamp", timestamp);

        // 5. Drop raw XML if configured
        if (config.isDropRawXml()) {
            event.delete(config.getSourceField());
        }
    }

    @Override
    public void prepareForShutdown() {}

    @Override
    public boolean isReadyForShutdown() { return true; }

    @Override
    public void shutdown() {}
}
