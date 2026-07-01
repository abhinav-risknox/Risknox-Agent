package org.opensearch.dataprepper.plugins.windowsevent;

import com.fasterxml.jackson.annotation.JsonProperty;

public class WindowsEventNormalizerConfig {

    private static final String DEFAULT_SOURCE_FIELD = "data";
    private static final boolean DEFAULT_DROP_RAW_XML = true;

    @JsonProperty("source_field")
    private String sourceField = DEFAULT_SOURCE_FIELD;

    @JsonProperty("drop_raw_xml")
    private boolean dropRawXml = DEFAULT_DROP_RAW_XML;

    public String getSourceField() {
        return sourceField;
    }

    public boolean isDropRawXml() {
        return dropRawXml;
    }
}
