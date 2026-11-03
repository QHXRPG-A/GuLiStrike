(function () {
    if (typeof JSON === 'undefined') {
        $.evalFile(new File('C:/Users/a/.codex/mcp-servers/mcp-aftereffects/node_modules/@kumoproductions/mcp-aftereffects/jsx/json2.jsx'));
    }
    var report = { project: app.project.file ? app.project.file.fsName : null, expressions: [], errors: [], footage: [] };
    function walk(group, path, comp, layer) {
        for (var k = 1; k <= group.numProperties; k++) {
            var property = group.property(k);
            var next = path.concat([k]);
            if (property.propertyType !== PropertyType.PROPERTY) {
                walk(property, next, comp, layer);
            } else if (property.canSetExpression && property.expression) {
                var record = { compId: comp.id, compName: comp.name, layer: layer.index, layerName: layer.name,
                    path: next, name: property.name, matchName: property.matchName,
                    enabled: property.expressionEnabled, expression: property.expression, error: property.expressionError };
                report.expressions.push(record);
                if (record.error) report.errors.push(record);
            }
        }
    }
    for (var i = 1; i <= app.project.numItems; i++) {
        var comp = app.project.item(i);
        if (comp instanceof FootageItem && comp.file) {
            var source = comp.mainSource;
            report.footage.push({ name: comp.name, file: comp.file.fsName, hasAlpha: source.hasAlpha,
                alphaMode: source.alphaMode === AlphaMode.PREMULTIPLIED ? 'premultiplied' : (source.alphaMode === AlphaMode.STRAIGHT ? 'straight' : 'ignore'),
                premulColor: source.premulColor, fps: comp.frameRate, hasAudio: comp.hasAudio });
        }
        if (!(comp instanceof CompItem)) continue;
        for (var j = 1; j <= comp.numLayers; j++) walk(comp.layer(j), [], comp, comp.layer(j));
    }
    $.global.HUDuiPROExpressionAudit = report;
    for (var n = 1; n <= app.project.numItems; n++) {
        var output = app.project.item(n);
        if (output.id === 32959) {
            output.comment = JSON.stringify({ expressionCount: report.expressions.length, errorCount: report.errors.length,
                errors: report.errors.slice(0, 8), footage: report.footage });
            break;
        }
    }
}());
