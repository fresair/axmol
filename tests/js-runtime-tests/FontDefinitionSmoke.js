/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Empty native Label fields only: this fixture never lays out text or creates a graphics device. */
var fontDefinitionSmokeReport = (function () {
    var checks = 0;
    function expect(condition, message) {
        ++checks;
        if (!condition) throw new Error('FontDefinition: ' + message);
    }
    var definition = new cc.FontDefinition();
    definition.fontName = 'Helvetica'; definition.fontSize = 23;
    definition.boundingWidth = 140; definition.boundingHeight = 67;
    definition.textAlign = 2; definition.verticalAlign = 1;
    definition.fillStyle = cc.color(23, 47, 83, 128);
    definition.strokeEnabled = true; definition.strokeStyle = cc.color(9, 14, 27, 255);
    definition.lineWidth = 2;
    definition.shadowEnabled = true; definition.shadowOffsetX = 3; definition.shadowOffsetY = -4;
    definition.shadowOpacity = 0.5; definition.shadowBlur = 1;
    var label = new cc.LabelTTF('', definition), actual = label.getTextDefinition();
    expect(label.getString() === '', 'empty native string');
    expect(label.getSystemFontName() === 'Helvetica', 'native font family');
    expect(label.getSystemFontSize() === 23, 'native font size');
    expect(label.getDimensions().width === 140 && label.getDimensions().height === 67, 'native bounds');
    expect(label.getHorizontalAlignment() === 2 && label.getVerticalAlignment() === 1, 'native alignment');
    var fill = label.getTextColor();
    expect(fill.r === 23 && fill.g === 47 && fill.b === 83 && fill.a === 128, 'native fill color');
    var outline = label._getStrokeStyle();
    expect(outline.r === 9 && outline.g === 14 && outline.b === 27 && label._getLineWidth() === 2,
        'native outline color and width');
    expect(label.isShadowEnabled(), 'native shadow enabled');
    expect(label._getShadowOffsetX() === 3 && label._getShadowOffsetY() === -4, 'native shadow offset');
    expect(Math.abs(label._getShadowOpacity() - 0.5) <= 1 / 255, 'native byte shadow opacity');
    // Both the original CCLabel and the current Label ignore blurRadius in enableShadow.
    // Keep the earlier blur=1 diagnostic; the supported native getter must report zero.
    expect(label._getShadowBlur() === 0 && actual.shadowBlur === 0, 'native ignored blur radius remains zero');
    expect(actual.fontName === 'Helvetica' && actual.fontSize === 23 && actual.boundingWidth === 140 &&
        actual.boundingHeight === 67 && actual.textAlign === 2 && actual.verticalAlign === 1, 'definition field round trip');
    expect(actual.strokeEnabled === true && actual.lineWidth === 2 && actual.strokeStyle.r === 9 &&
        actual.strokeStyle.g === 14 && actual.strokeStyle.b === 27, 'outline definition round trip');
    expect(actual.shadowEnabled === true && actual.shadowOffsetX === 3 && actual.shadowOffsetY === -4 &&
        Math.abs(actual.shadowOpacity - 0.5) <= 1 / 255, 'shadow definition round trip');
    var defaults = new cc.FontDefinition(), defaultLabel = new cc.LabelTTF('', defaults);
    expect(defaults.shadowBlur === 0 && defaultLabel._getShadowBlur() === 0, 'original default blur radius');
    expect(new cc.LabelTTF(37, defaults).getString() === '37', 'legacy numeric label string');
    expect(new cc.LabelTTF(true, defaults).getString() === 'true', 'legacy true label string');
    // The original JS constructor applies text || '' before native conversion.
    expect(new cc.LabelTTF(false, defaults).getString() === '', 'original false constructor text becomes empty');
    expect(new cc.LabelTTF(null, defaults).getString() === '', 'legacy null label string');
    expect(new cc.LabelTTF(undefined, defaults).getString() === '', 'legacy undefined label string');
    defaultLabel.setString(false);
    expect(defaultLabel.getString() === 'false', 'direct native boolean string conversion');
    defaultLabel.setString(24);
    expect(defaultLabel.getString() === '24', 'direct native numeric string conversion');
    defaultLabel.setString(null);
    expect(defaultLabel.getString() === '', 'direct native null string conversion');
    expect(defaultLabel.initWithStringAndTextDefinition(false, defaults) === true &&
        defaultLabel.getString() === 'false', 'direct native FontDefinition init preserves false text');
    cc.log('FONT_DEFINITION_SMOKE_PASS ' + checks + ' checks');
    return {phase: 'empty-label-font-definition-regression', passed: true, checks: checks,
        requestedBlur: 1, nativeBlur: label._getShadowBlur(), originalDefaultBlur: defaults.shadowBlur,
        graphicsDeviceCreated: false};
})();

