(function () {
    function walk(group) {
        for (var i = 1; i <= group.numProperties; i++) {
            var property = group.property(i);
            if (property.propertyType !== PropertyType.PROPERTY) {
                walk(property);
            } else if (property.canSetExpression && property.expression) {
                var before = property.expression;
                var after = before.replace(/(\.effect\("[^"\r\n]+"\))\("Color"\)/g, '$1("ADBE Color Control-0001")');
                after = after.split('.effect("Color Control")').join('.effect("ADBE Color Control")');
                after = after.split('.effect("Slider Control")').join('.effect("ADBE Slider Control")');
                after = after.split('("Slider")').join('("ADBE Slider Control-0001")');
                if (before !== after) {
                    var enabled = property.expressionEnabled;
                    property.expression = after;
                    property.expressionEnabled = enabled;
                }
            }
        }
    }
    for (var c = 1; c <= app.project.numItems; c++) {
        var comp = app.project.item(c);
        if (!(comp instanceof CompItem)) continue;
        for (var l = 1; l <= comp.numLayers; l++) walk(comp.layer(l));
    }
    $.evalFile(new File('D:/UE5.7/test1/Scripts/Art/HUDuiPRO/audit_expressions.jsx'));
}());
