--[[ pandoc Lua filter for the PDF of summary.md (see summary-pdf.yaml).

The compile chart is tall and wide: its section gets 2 cm margins, the rest
of the document LaTeX's default ones. LaTeX can't be written in summary.md
itself (GitHub and GitLab would show it), so this filter adds it:
\newgeometry before the section's heading, \restoregeometry after its
chart. Both start a new page. The section is found by the identifier pandoc
makes from its heading, "Compile on the device: time and heap"
(summary.md.j2): keep the two in step.
]]

local SECTION = "compile-on-the-device"  -- the start of the heading's identifier

local function latex(s)
  return pandoc.RawBlock("latex", s)
end

local function has_image(block)
  local found = false
  if block.t == "Para" or block.t == "Plain" or block.t == "Figure" then
    pandoc.walk_block(block, { Image = function() found = true end })
  end
  return found
end

function Pandoc(doc)
  -- geometry with "pass": it leaves the default layout alone, but
  -- \newgeometry and \restoregeometry work
  local includes = doc.meta["header-includes"] or pandoc.MetaList({})
  if includes.t ~= "MetaList" then
    includes = pandoc.MetaList({ includes })
  end
  includes:insert(pandoc.MetaBlocks({ latex("\\usepackage[pass]{geometry}") }))
  doc.meta["header-includes"] = includes

  local out, open = pandoc.List(), false
  for _, b in ipairs(doc.blocks) do
    if b.t == "Header" and open then
      out:insert(latex("\\restoregeometry"))  -- no chart before the next section
      open = false
    end
    if b.t == "Header" and b.identifier:sub(1, #SECTION) == SECTION then
      out:insert(latex("\\newgeometry{margin=2cm}"))
      open = true
    end
    out:insert(b)
    if open and has_image(b) then
      out:insert(latex("\\restoregeometry"))
      open = false
    end
  end
  if open then
    out:insert(latex("\\restoregeometry"))
  end
  doc.blocks = out
  return doc
end
