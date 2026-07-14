package org.opensearch.dataprepper.plugins.windowsevent;

import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;
import java.io.StringReader;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Parses Windows Event XML (as rendered by EvtRender/EvtRenderEventXml) into
 * two structured maps:
 * <ul>
 *   <li><b>systemFields</b> — metadata from the &lt;System&gt; block
 *       (EventID, Provider_Name, Channel, Computer, Level, etc.)</li>
 *   <li><b>eventData</b> — payload fields from the &lt;EventData&gt; or
 *       &lt;UserData&gt; block, preserving original PascalCase names exactly
 *       as they appear in the XML {@code <Data Name="...">} attributes.</li>
 * </ul>
 *
 * <p>All values are returned as strings. Numeric conversion (e.g. EventID → int)
 * is the caller's responsibility.</p>
 */
public class XmlParserUtils {

    private static final XMLInputFactory FACTORY = XMLInputFactory.newInstance();
    static {
        // Prevent XXE and external entity resolution
        FACTORY.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, false);
        FACTORY.setProperty(XMLInputFactory.SUPPORT_DTD, false);
        // Handle namespace-prefixed XML from EvtRender
        FACTORY.setProperty(XMLInputFactory.IS_NAMESPACE_AWARE, true);
    }

    /**
     * Structured result of parsing a Windows Event XML string.
     */
    public static class ParsedEvent {
        /** System-level metadata (EventID, Provider_Name, Channel, Computer, etc.) */
        public final Map<String, String> systemFields;

        /** EventData/UserData payload fields with original PascalCase names */
        public final Map<String, String> eventData;

        public ParsedEvent(Map<String, String> systemFields, Map<String, String> eventData) {
            this.systemFields = systemFields;
            this.eventData = eventData;
        }
    }

    // Tags within <System> that carry values as attributes (not text content)
    private static final String TAG_PROVIDER = "Provider";
    private static final String TAG_TIME_CREATED = "TimeCreated";
    private static final String TAG_EXECUTION = "Execution";
    private static final String TAG_SECURITY = "Security";
    private static final String TAG_CORRELATION = "Correlation";

    /**
     * Parse a Windows Event XML string into a {@link ParsedEvent}.
     *
     * @param xml  the raw XML string from EvtRender (may include xmlns namespace)
     * @return parsed event with separate system and eventData maps; never null
     */
    public static ParsedEvent parse(String xml) {
        Map<String, String> systemFields = new LinkedHashMap<>();
        Map<String, String> eventData = new LinkedHashMap<>();

        if (xml == null || xml.trim().isEmpty()) {
            return new ParsedEvent(systemFields, eventData);
        }

        try {
            XMLStreamReader reader = FACTORY.createXMLStreamReader(new StringReader(xml));

            boolean inSystem = false;
            boolean inEventData = false;   // covers both <EventData> and <UserData>
            boolean inDataTag = false;
            String currentDataName = null;
            StringBuilder currentText = new StringBuilder();
            int unnamedDataIndex = 0;

            while (reader.hasNext()) {
                int event = reader.next();

                switch (event) {
                    case XMLStreamConstants.START_ELEMENT:
                        String tagName = reader.getLocalName();
                        currentText.setLength(0);

                        if ("System".equals(tagName)) {
                            inSystem = true;
                        } else if ("EventData".equals(tagName) || "UserData".equals(tagName)) {
                            inSystem = false;
                            inEventData = true;
                            unnamedDataIndex = 0;
                        } else if (inSystem) {
                            extractSystemAttributes(reader, tagName, systemFields);
                        } else if (inEventData && "Data".equals(tagName)) {
                            inDataTag = true;
                            currentDataName = null;
                            for (int i = 0; i < reader.getAttributeCount(); i++) {
                                if ("Name".equals(reader.getAttributeLocalName(i))) {
                                    currentDataName = reader.getAttributeValue(i);
                                    break;
                                }
                            }
                            // If no Name attribute, assign a numbered key
                            if (currentDataName == null) {
                                currentDataName = "Data_" + unnamedDataIndex++;
                            }
                        }
                        break;

                    case XMLStreamConstants.CHARACTERS:
                    case XMLStreamConstants.CDATA:
                        currentText.append(reader.getText());
                        break;

                    case XMLStreamConstants.END_ELEMENT:
                        String endTag = reader.getLocalName();

                        if ("System".equals(endTag)) {
                            inSystem = false;
                        } else if ("EventData".equals(endTag) || "UserData".equals(endTag)) {
                            inEventData = false;
                        } else if ("Data".equals(endTag) && inDataTag) {
                            // Store eventData field with original name
                            if (currentDataName != null) {
                                String value = currentText.toString().trim();
                                eventData.put(currentDataName, value);
                            }
                            inDataTag = false;
                            currentDataName = null;
                        } else if (inSystem) {
                            // System tags with text content: EventID, Level, Task, etc.
                            String text = currentText.toString().trim();
                            if (!text.isEmpty()) {
                                systemFields.put(endTag, text);
                            }
                        }
                        currentText.setLength(0);
                        break;
                }
            }
            reader.close();
        } catch (XMLStreamException e) {
            // If XML is malformed, return whatever was successfully extracted
        }

        return new ParsedEvent(systemFields, eventData);
    }

    /**
     * Extract attributes from specific System sub-elements.
     * E.g. {@code <Provider Name="..." Guid="..."/>} becomes
     * Provider_Name, Provider_Guid in systemFields.
     */
    private static void extractSystemAttributes(XMLStreamReader reader, String tagName,
                                                 Map<String, String> systemFields) {
        switch (tagName) {
            case TAG_PROVIDER:
            case TAG_TIME_CREATED:
            case TAG_EXECUTION:
            case TAG_SECURITY:
            case TAG_CORRELATION:
                for (int i = 0; i < reader.getAttributeCount(); i++) {
                    String attrName = tagName + "_" + reader.getAttributeLocalName(i);
                    String attrValue = reader.getAttributeValue(i);
                    if (attrValue != null && !attrValue.isEmpty()) {
                        systemFields.put(attrName, attrValue);
                    }
                }
                break;
            default:
                // Other system tags (EventID, Level, etc.) are text-content based,
                // handled in END_ELEMENT
                break;
        }
    }

    // ── Legacy compatibility method ──────────────────────────────────────────
    // Kept so existing tests don't break during migration. Delegates to parse().

    /**
     * @deprecated Use {@link #parse(String)} instead.
     */
    @Deprecated
    public static Map<String, Object> parseEventData(String xml) {
        ParsedEvent parsed = parse(xml);
        Map<String, Object> flat = new LinkedHashMap<>();
        flat.putAll(parsed.systemFields);
        flat.putAll(parsed.eventData);
        return flat;
    }
}
