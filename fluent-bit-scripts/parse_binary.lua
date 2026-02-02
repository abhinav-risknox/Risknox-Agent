function parse_binary_message(tag, timestamp, record)
    -- Fluent Bit TCP input gives us raw bytes in the 'log' field
    local raw = record["log"]
    
    if not raw or #raw < 36 then
        return -1, timestamp, record  -- Invalid message, drop it
    end
    
    -- Parse length (first 4 bytes, big-endian)
    local b1, b2, b3, b4 = string.byte(raw, 1, 4)
    local payload_length = (b1 * 16777216) + (b2 * 65536) + (b3 * 256) + b4
    
    -- Verify total length matches
    if #raw ~= (4 + payload_length) then
        return -1, timestamp, record  -- Length mismatch, drop
    end
    
    -- Parse agent ID (bytes 5-36, null-terminated UTF-8)
    local agent_id_raw = string.sub(raw, 5, 36)
    local agent_id = agent_id_raw:match("^[^%z]+") or "unknown"
    record["agent_id"] = agent_id
    
    -- Parse XML (bytes 37 to end)
    if #raw > 36 then
        local xml = string.sub(raw, 37)
        record["xml"] = xml
    else
        record["xml"] = ""
    end
    
    -- Remove raw log field (no longer needed)
    record["log"] = nil
    
    -- Add metadata
    record["protocol"] = "binary"
    record["received_at"] = os.date("!%Y-%m-%dT%H:%M:%SZ")
    
    return 1, timestamp, record
end
