// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.Codec
import java.util.Base64

/**
 * A backup of the FM-1's projects in the web editor's file format (web/EDITOR_PROTOCOL.md v6: `{format: "sloop-backup",
 * version: 1, firmware, date, objects: [{id, len, crc, data (base64)}]}`), so a backup made on the phone restores
 * from the browser and the other way round. Lengths and CRCs are checked on reading.
 */
class Backup(val firmware: String, val date: String, val objects: Map<Int, ByteArray>) {
    fun toJson(): String {
        val sb = StringBuilder()
        sb.append("{\"format\": \"sloop-backup\", \"version\": 1, \"firmware\": \"").append(esc(firmware))
            .append("\", \"date\": \"").append(esc(date)).append("\", \"objects\": [")
        objects.entries.sortedBy { it.key }.forEachIndexed { k, (id, d) ->
            if (k > 0) sb.append(", ")
            sb.append("{\"id\": ").append(id).append(", \"len\": ").append(d.size).append(", \"crc\": ").append(Codec.crc32(d))
                .append(", \"data\": \"").append(Base64.getEncoder().encodeToString(d)).append("\"}")
        }
        return sb.append("]}\n").toString()
    }

    /** the projects in it that are BOTANICA's (BTP1): object id -> bytes */
    fun projects(): Map<Int, ByteArray> = objects.filter { (id, d) -> id in PROJECT_IDS && Btp1.isBtp1(d) }

    companion object {
        /** the working project and the sections A..D */
        val PROJECT_IDS = listOf(0, 2, 3, 4, 5)

        private fun esc(s: String) = s.replace("\\", "\\\\").replace("\"", "\\\"")

        /** the string value of "[key]" (the first), unescaped; "" when there is none */
        private fun str(json: String, key: String): String {
            var i = json.indexOf("\"$key\"")
            if (i < 0) return ""
            i = json.indexOf(':', i + key.length + 2)
            if (i < 0) return ""
            i++
            while (i < json.length && json[i].isWhitespace()) i++
            if (i >= json.length || json[i] != '"') return ""
            val sb = StringBuilder()
            i++
            while (i < json.length && json[i] != '"') {
                if (json[i] == '\\' && i + 1 < json.length) i++
                sb.append(json[i++])
            }
            return sb.toString()
        }

        fun fromJson(json: String): Backup {
            if (str(json, "format") != "sloop-backup") throw IllegalArgumentException("not an FM-1 backup")
            val objs = LinkedHashMap<Int, ByteArray>()
            val obj = Regex("\\{[^{}]*\"id\"\\s*:\\s*(\\d+)[^{}]*\\}")
            for (m in obj.findAll(json)) {
                val o = m.value
                val id = m.groupValues[1].toInt()
                val len = Regex("\"len\"\\s*:\\s*(\\d+)").find(o)?.groupValues?.get(1)?.toInt() ?: -1
                val crc = Regex("\"crc\"\\s*:\\s*(\\d+)").find(o)?.groupValues?.get(1)?.toLong() ?: -1
                val d = Base64.getDecoder().decode(str(o, "data"))
                if (d.size != len) throw IllegalArgumentException("object $id: ${d.size} bytes, the file says $len")
                if (Codec.crc32(d) != crc) throw IllegalArgumentException("object $id: CRC mismatch")
                objs[id] = d
            }
            return Backup(str(json, "firmware"), str(json, "date"), objs)
        }
    }
}
