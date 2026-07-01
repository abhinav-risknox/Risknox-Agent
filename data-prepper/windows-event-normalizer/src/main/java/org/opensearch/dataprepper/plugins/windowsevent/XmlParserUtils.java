package org.opensearch.dataprepper.plugins.windowsevent;

import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;
import java.io.StringReader;
import java.util.HashMap;
import java.util.Map;

public class XmlParserUtils {

    private static final XMLInputFactory FACTORY = XMLInputFactory.newInstance();
    static {
        // Prevent XXE and external entity resolution
        FACTORY.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, false);
        FACTORY.setProperty(XMLInputFactory.SUPPORT_DTD, false);
    }

    /**
     * Parses a Windows Event XML string into a flat key-value map.
     * System tags become direct keys (e.g., "EventID").
     * EventData <Data Name="Key">Value</Data> become direct keys (e.g., "Key").
     * System tag attributes become prefixed keys (e.g., "TimeCreated_SystemTime").
     */
    public static Map<String, Object> parseEventData(String xml) {
        Map<String, Object> parsedData = new HashMap<>();
        if (xml == null || xml.trim().isEmpty()) {
            return parsedData;
        }

        try {
            XMLStreamReader reader = FACTORY.createXMLStreamReader(new StringReader(xml));
            
            String currentDataName = null;
            StringBuilder currentText = new StringBuilder();
            boolean inDataTag = false;
            boolean inSystemTag = false;

            while (reader.hasNext()) {
                int event = reader.next();

                switch (event) {
                    case XMLStreamConstants.START_ELEMENT:
                        String tagName = reader.getLocalName();
                        currentText.setLength(0); // Reset text buffer for new element
                        
                        if ("System".equals(tagName)) {
                            inSystemTag = true;
                        } else if ("EventData".equals(tagName) || "UserData".equals(tagName)) {
                            inSystemTag = false;
                        } else if ("Data".equals(tagName)) {
                            inDataTag = true;
                            for (int i = 0; i < reader.getAttributeCount(); i++) {
                                if ("Name".equals(reader.getAttributeLocalName(i))) {
                                    currentDataName = reader.getAttributeValue(i);
                                    break;
                                }
                            }
                        } else if (inSystemTag) {
                            // Extract attributes for specific System tags
                            if ("Provider".equals(tagName) || "TimeCreated".equals(tagName) || "Execution".equals(tagName) || "Security".equals(tagName) || "Correlation".equals(tagName)) {
                                for (int i = 0; i < reader.getAttributeCount(); i++) {
                                    String attrName = tagName + "_" + reader.getAttributeLocalName(i);
                                    parsedData.put(attrName, tryParseNumber(reader.getAttributeValue(i)));
                                }
                            }
                        }
                        break;

                    case XMLStreamConstants.CHARACTERS:
                    case XMLStreamConstants.CDATA:
                        currentText.append(reader.getText());
                        break;

                    case XMLStreamConstants.END_ELEMENT:
                        String endTagName = reader.getLocalName();
                        
                        if ("System".equals(endTagName)) {
                            inSystemTag = false;
                        } else if ("Data".equals(endTagName) && inDataTag) {
                            if (currentDataName != null) {
                                parsedData.put(currentDataName, tryParseNumber(currentText.toString().trim()));
                            }
                            inDataTag = false;
                            currentDataName = null;
                        } else if (inSystemTag && currentText.length() > 0) {
                            // Extract text content of standard System tags like EventID, Level, Channel, Computer
                            String text = currentText.toString().trim();
                            if (!text.isEmpty()) {
                                parsedData.put(endTagName, tryParseNumber(text));
                            }
                        }
                        currentText.setLength(0);
                        break;
                }
            }
            reader.close();
        } catch (XMLStreamException e) {
            // If XML is malformed, return whatever we successfully extracted
        }

        return parsedData;
    }

    private static Object tryParseNumber(String value) {
        // Return all values as strings — winlog.event_data fields are keyword-mapped
        // in OpenSearch. System fields like EventID are extracted separately as
        // integers by the normalizer's safeGetInt().
        return value;
    }
}
