-- Lua script to parse Windows Event Log XML to ECS (Elastic Common Schema) format
-- This creates winlog.* fields compatible with Sigma rules and Elastic/OpenSearch Security

function parse_event_xml(tag, timestamp, record)
    local data = record["data"]
    
    if not data then
        return 0, 0, 0
    end
    
    -- Detect if it's JSON (FIM or System Info)
    if data:sub(1,1) == "{" then
        -- It's JSON, attempt to parse (using a simple pattern extractor if cjson not present)
        -- In Fluent Bit, we usually have cjson available via require
        local status, json = pcall(function() return require("cjson").decode(data) end)
        
        if status and json then
            -- Map JSON fields to record
            if json.type == "fim" then
                record["event.category"] = "file"
                record["event.type"] = "change"
                record["file.path"] = json.path
                record["file.name"] = json.path:match("([^\\]+)$") or json.path
                record["winlog.channel"] = "FIM"
                record["message"] = "File " .. (json.change_type or "changed") .. ": " .. json.path
                
                if json.new_hash then record["hash.sha256"] = json.new_hash end
                if json.old_hash then record["hash.old_sha256"] = json.old_hash end
            elseif json.type == "system_info" then
                record["event.category"] = "host"
                record["winlog.channel"] = "SystemInfo"
                
                -- Map OS Info to ECS
                if json.os_info then
                    local os = json.os_info
                    record["host.os.name"] = os.os_name
                    record["host.os.version"] = os.version
                    record["host.os.build"] = os.build
                    record["host.os.full"] = (os.os_name or "") .. " " .. (os.version or "")
                    record["host.hostname"] = os.hostname
                    record["host.architecture"] = os.architecture
                    record["host.domain"] = os.domain
                    record["host.uptime"] = os.uptime_seconds
                    record["host.manufacturer"] = os.manufacturer
                    record["host.model"] = os.model
                    
                    -- Also set host.name for general ECS compatibility
                    if os.hostname then record["host.name"] = os.hostname end
                end
                
                -- Store applications and connections
                if json.installed_applications then 
                    record["host.installed_apps"] = json.installed_applications 
                end
                
                if json.network_connections then
                    -- These are already split into listening and established in the agent
                    record["host.network.listening"] = json.network_connections.listening_ports
                    record["host.network.established"] = json.network_connections.established_connections
                end
                
                record["message"] = "System information collected for " .. (record["host.hostname"] or "unknown host")
            end
            
            -- Set common fields
            if json.timestamp or json.collected_at then
                record["@timestamp"] = json.timestamp or json.collected_at
            end
            
            record["event.original"] = data
            record["event.kind"] = "event"
            
            return 1, timestamp, record
        end
    end

    -- ========== XML Parsing logic (existing) ==========
    local xml = data
    
    -- Provider Name
    local provider_name = xml:match("Provider%s+Name='([^']+)'")
    if provider_name then
        record["winlog.provider_name"] = provider_name
    end
    
    -- Provider GUID
    local provider_guid = xml:match("Provider%s+[^>]*Guid='{([^}]+)}'")
    if provider_guid then
        record["winlog.provider_guid"] = provider_guid
    end
    
    -- Event ID
    local event_id = xml:match("<EventID[^>]*>(%d+)</EventID>")
    if event_id then
        record["winlog.event_id"] = tonumber(event_id)
    end
    
    -- Channel
    local channel = xml:match("<Channel>([^<]+)</Channel>")
    if channel then
        record["winlog.channel"] = channel
    end
    
    -- Computer Name
    local computer = xml:match("<Computer>([^<]+)</Computer>")
    if computer then
        record["winlog.computer_name"] = computer
        record["host.name"] = computer
        record["host.hostname"] = computer
    end
    
    -- Level
    local level = xml:match("<Level>(%d+)</Level>")
    if level then
        local level_num = tonumber(level)
        record["winlog.level"] = level_num
        -- Map to severity string
        if level_num == 1 then record["log.level"] = "critical"
        elseif level_num == 2 then record["log.level"] = "error"
        elseif level_num == 3 then record["log.level"] = "warning"
        elseif level_num == 4 then record["log.level"] = "information"
        else record["log.level"] = "verbose"
        end
    end
    
    -- Task
    local task = xml:match("<Task>(%d+)</Task>")
    if task then
        record["winlog.task"] = tonumber(task)
    end
    
    -- Opcode
    local opcode = xml:match("<Opcode>(%d+)</Opcode>")
    if opcode then
        record["winlog.opcode"] = tonumber(opcode)
    end
    
    -- Keywords (hex string)
    local keywords = xml:match("<Keywords>([^<]+)</Keywords>")
    if keywords then
        record["winlog.keywords"] = keywords
    end
    
    -- TimeCreated
    local time_created = xml:match("TimeCreated%s+SystemTime='([^']+)'")
    if time_created then
        record["winlog.time_created"] = time_created
        -- Also use as @timestamp
        record["@timestamp"] = time_created
    end
    
    -- EventRecordID
    local record_id = xml:match("<EventRecordID>(%d+)</EventRecordID>")
    if record_id then
        record["winlog.record_id"] = tonumber(record_id)
        record["event.code"] = record_id
    end
    
    -- Execution ProcessID and ThreadID  
    local process_id = xml:match("Execution%s+ProcessID='(%d+)'")
    if process_id then
        record["winlog.process.pid"] = tonumber(process_id)
        record["process.pid"] = tonumber(process_id)
    end
    
    local thread_id = xml:match("ThreadID='(%d+)'")
    if thread_id then
        record["winlog.process.thread.id"] = tonumber(thread_id)
    end
    
    -- Security UserID (SID)
    local user_sid = xml:match("Security%s+UserID='([^']+)'")
    if user_sid then
        record["winlog.user.identifier"] = user_sid
        record["user.id"] = user_sid
    end
    
    -- Version
    local version = xml:match("<Version>(%d+)</Version>")
    if version then
        record["winlog.version"] = tonumber(version)
    end
    
    -- ========== EventData Section (winlog.event_data.*) ==========
    -- Extract all EventData fields as key-value pairs
    local event_data = {}
    
    -- Pattern: <Data Name='key'>value</Data> (Windows uses single quotes)
    for name, value in xml:gmatch("<Data%s+Name='([^']+)'>([^<]*)</Data>") do
        event_data[name] = value
        -- Also create individual fields
        record["winlog.event_data." .. name] = value
    end
    
    -- Pattern 2: <Data>value</Data> (unnamed, indexed)
    local unnamed_index = 1
    for value in xml:gmatch("<Data>([^<]+)</Data>") do
        event_data["param" .. unnamed_index] = value
        record["winlog.event_data.param" .. unnamed_index] = value
        unnamed_index = unnamed_index + 1
    end
    
    -- ========== Special Field Mappings for Common Fields ==========
    
    -- Process fields (Sysmon Event ID 1, 5, etc.)
    if event_data["Image"] then
        record["process.executable"] = event_data["Image"]
        record["process.name"] = event_data["Image"]:match("([^\\]+)$") or event_data["Image"]
    end
    
    if event_data["CommandLine"] or event_data["Commandline"] then
        local cmdline = event_data["CommandLine"] or event_data["Commandline"]
        record["process.command_line"] = cmdline
    end
    
    if event_data["ParentImage"] then
        record["process.parent.executable"] = event_data["ParentImage"]
        record["process.parent.name"] = event_data["ParentImage"]:match("([^\\]+)$") or event_data["ParentImage"]
    end
    
    if event_data["ParentProcessId"] then
        record["process.parent.pid"] = tonumber(event_data["ParentProcessId"])
    end
    
    if event_data["User"] then
        record["winlog.user.name"] = event_data["User"]
        record["user.name"] = event_data["User"]
    end
    
    -- Network fields (Sysmon Event ID 3, Security 5156, etc.)
    if event_data["SourceIp"] or event_data["IpAddress"] then
        local src_ip = event_data["SourceIp"] or event_data["IpAddress"]
        record["source.ip"] = src_ip
    end
    
    if event_data["DestinationIp"] or event_data["DestAddress"] then
        local dst_ip = event_data["DestinationIp"] or event_data["DestAddress"]
        record["destination.ip"] = dst_ip
    end
    
    if event_data["SourcePort"] then
        record["source.port"] = tonumber(event_data["SourcePort"])
    end
    
    if event_data["DestinationPort"] or event_data["TargetPort"] then
        local dst_port = event_data["DestinationPort"] or event_data["TargetPort"]
        record["destination.port"] = tonumber(dst_port)
    end
    
    -- File fields
    if event_data["TargetFilename"] or event_data["FileName"] then
        local filename = event_data["TargetFilename"] or event_data["FileName"]
        record["file.path"] = filename
        record["file.name"] = filename:match("([^\\]+)$") or filename
    end
    
    -- Hash fields (Sysmon)
    if event_data["Hashes"] then
        local hashes = event_data["Hashes"]
        -- Parse MD5
        local md5 = hashes:match("MD5=([^,]+)")
        if md5 then record["hash.md5"] = md5 end
        
        -- Parse SHA1
        local sha1 = hashes:match("SHA1=([^,]+)")
        if sha1 then record["hash.sha1"] = sha1 end
        
        -- Parse SHA256
        local sha256 = hashes:match("SHA256=([^,]+)")
        if sha256 then record["hash.sha256"] = sha256 end
        
        -- Parse IMPHASH
        local imphash = hashes:match("IMPHASH=([^,]+)")
        if imphash then record["hash.imphash"] = imphash end
    end
    
    -- Security Event Fields (4624, 4625, 4672, etc.)
    if event_data["TargetUserName"] then
        record["winlog.user.name"] = event_data["TargetUserName"]
        record["user.name"] = event_data["TargetUserName"]
    end
    
    if event_data["TargetDomainName"] or event_data["SubjectDomainName"] then
        local domain = event_data["TargetDomainName"] or event_data["SubjectDomainName"]
        record["user.domain"] = domain
    end
    
    if event_data["TargetUserSid"] or event_data["SubjectUserSid"] then
        local sid = event_data["TargetUserSid"] or event_data["SubjectUserSid"]
        record["user.id"] = sid
    end
    
    if event_data["LogonType"] then
        record["winlog.logon.type"] = event_data["LogonType"]
    end
    
    if event_data["WorkstationName"] or event_data["Workstation"] then
        local workstation = event_data["WorkstationName"] or event_data["Workstation"]
        record["source.domain"] = workstation
    end
    
    -- Registry fields (Sysmon Event ID 12, 13, 14)
    if event_data["TargetObject"] then
        record["registry.path"] = event_data["TargetObject"]
    end
    
    -- DNS fields (Sysmon Event ID 22)
    if event_data["QueryName"] then
        record["dns.question.name"] = event_data["QueryName"]
    end
    
    -- Store complete event_data object
    if next(event_data) ~= nil then
        record["event_data"] = event_data
    end
    
    -- ========== Message Field ==========
    -- Some events have a Message or Description
    local message = xml:match("<Message>([^<]+)</Message>")
    if message then
        record["message"] = message
        record["windows.message"] = message
    end
    
    -- Keep raw XML
    record["event.original"] = xml
    
    -- Set event category
    record["event.kind"] = "event"
    record["event.category"] = "process"  -- Default, should be refined based on event type
    
    return 1, timestamp, record
end
