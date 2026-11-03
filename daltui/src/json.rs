//! Minimal JSON reader for the flight console replies (`json <cmd>`, `gui_state`).
//! Numbers are f64, objects keep insertion order. No writer is needed.

#[derive(Debug, Clone, PartialEq)]
pub enum Json {
    Null,
    Bool(bool),
    Num(f64),
    Str(String),
    Arr(Vec<Json>),
    Obj(Vec<(String, Json)>),
}

impl Json {
    pub fn parse(s: &str) -> Result<Json, String> {
        let mut p = P {
            b: s.as_bytes(),
            i: 0,
        };
        let v = p.value(0)?;
        p.ws();
        if p.i != p.b.len() {
            return Err(format!("trailing data at byte {}", p.i));
        }
        Ok(v)
    }

    pub fn get(&self, key: &str) -> &Json {
        if let Json::Obj(o) = self {
            for (k, v) in o {
                if k == key {
                    return v;
                }
            }
        }
        &Json::Null
    }

    pub fn at(&self, i: usize) -> &Json {
        match self {
            Json::Arr(a) => a.get(i).unwrap_or(&Json::Null),
            _ => &Json::Null,
        }
    }

    pub fn is_null(&self) -> bool {
        matches!(self, Json::Null)
    }
    pub fn f64(&self) -> Option<f64> {
        match self {
            Json::Num(n) => Some(*n),
            Json::Bool(b) => Some(*b as u8 as f64),
            _ => None,
        }
    }
    pub fn i64(&self) -> Option<i64> {
        self.f64().map(|n| n as i64)
    }
    pub fn bool(&self) -> Option<bool> {
        match self {
            Json::Bool(b) => Some(*b),
            Json::Num(n) => Some(*n != 0.0),
            _ => None,
        }
    }
    pub fn str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s),
            _ => None,
        }
    }
}

struct P<'a> {
    b: &'a [u8],
    i: usize,
}

impl P<'_> {
    fn ws(&mut self) {
        while self.i < self.b.len() && self.b[self.i].is_ascii_whitespace() {
            self.i += 1;
        }
    }

    fn lit(&mut self, word: &str, v: Json) -> Result<Json, String> {
        if self.b[self.i..].starts_with(word.as_bytes()) {
            self.i += word.len();
            Ok(v)
        } else {
            Err(format!("bad literal at byte {}", self.i))
        }
    }

    fn value(&mut self, depth: usize) -> Result<Json, String> {
        if depth > 32 {
            return Err("nesting too deep".into());
        }
        self.ws();
        match self.b.get(self.i) {
            None => Err("unexpected end".into()),
            Some(b'{') => {
                self.i += 1;
                let mut o = Vec::new();
                loop {
                    self.ws();
                    if self.b.get(self.i) == Some(&b'}') {
                        self.i += 1;
                        return Ok(Json::Obj(o));
                    }
                    if !o.is_empty() {
                        if self.b.get(self.i) != Some(&b',') {
                            return Err(format!("expected ',' at byte {}", self.i));
                        }
                        self.i += 1;
                        self.ws();
                    }
                    let k = self.string()?;
                    self.ws();
                    if self.b.get(self.i) != Some(&b':') {
                        return Err(format!("expected ':' at byte {}", self.i));
                    }
                    self.i += 1;
                    o.push((k, self.value(depth + 1)?));
                }
            }
            Some(b'[') => {
                self.i += 1;
                let mut a = Vec::new();
                loop {
                    self.ws();
                    if self.b.get(self.i) == Some(&b']') {
                        self.i += 1;
                        return Ok(Json::Arr(a));
                    }
                    if !a.is_empty() {
                        if self.b.get(self.i) != Some(&b',') {
                            return Err(format!("expected ',' at byte {}", self.i));
                        }
                        self.i += 1;
                    }
                    a.push(self.value(depth + 1)?);
                }
            }
            Some(b'"') => Ok(Json::Str(self.string()?)),
            Some(b't') => self.lit("true", Json::Bool(true)),
            Some(b'f') => self.lit("false", Json::Bool(false)),
            Some(b'n') => self.lit("null", Json::Null),
            Some(_) => {
                let s = self.i;
                while self.i < self.b.len()
                    && matches!(
                        self.b[self.i],
                        b'-' | b'+' | b'.' | b'e' | b'E' | b'0'..=b'9'
                    )
                {
                    self.i += 1;
                }
                std::str::from_utf8(&self.b[s..self.i])
                    .ok()
                    .and_then(|t| t.parse::<f64>().ok())
                    .map(Json::Num)
                    .ok_or_else(|| format!("bad value at byte {s}"))
            }
        }
    }

    fn string(&mut self) -> Result<String, String> {
        if self.b.get(self.i) != Some(&b'"') {
            return Err(format!("expected string at byte {}", self.i));
        }
        self.i += 1;
        let mut out: Vec<u8> = Vec::new();
        loop {
            let c = *self.b.get(self.i).ok_or("unterminated string")?;
            self.i += 1;
            match c {
                b'"' => return String::from_utf8(out).map_err(|e| e.to_string()),
                b'\\' => {
                    let e = *self.b.get(self.i).ok_or("bad escape")?;
                    self.i += 1;
                    match e {
                        b'n' => out.push(b'\n'),
                        b't' => out.push(b'\t'),
                        b'r' => out.push(b'\r'),
                        b'b' => out.push(8),
                        b'f' => out.push(12),
                        b'u' => {
                            let h = self.b.get(self.i..self.i + 4).ok_or("bad \\u escape")?;
                            let n = u32::from_str_radix(
                                std::str::from_utf8(h).map_err(|e| e.to_string())?,
                                16,
                            )
                            .map_err(|e| e.to_string())?;
                            self.i += 4;
                            let ch = char::from_u32(n).unwrap_or('?');
                            let mut buf = [0u8; 4];
                            out.extend_from_slice(ch.encode_utf8(&mut buf).as_bytes());
                        }
                        other => out.push(other),
                    }
                }
                _ => out.push(c),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_console_reply() {
        let j = Json::parse(
            r#"{"ok":true,"type":4,"text":"a\nb","payload_hex":"ff01","x":[1,-2.5,null]}"#,
        )
        .unwrap();
        assert_eq!(j.get("ok").bool(), Some(true));
        assert_eq!(j.get("type").i64(), Some(4));
        assert_eq!(j.get("text").str(), Some("a\nb"));
        assert_eq!(j.get("x").at(1).f64(), Some(-2.5));
        assert!(j.get("x").at(2).is_null());
        assert!(j.get("missing").is_null());
    }

    #[test]
    fn rejects_garbage() {
        assert!(Json::parse("{\"a\":").is_err());
        assert!(Json::parse("{} x").is_err());
    }
}
