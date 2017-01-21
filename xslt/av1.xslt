<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:include href="commonTemplates.xsl"/>
  <xsl:strip-space elements="*"/>
  <xsl:param name="timeField" select="timeField"/>
  <xsl:param name="depthField" select="depthField"/>
  <xsl:param name="date" select="date"/>
  <xsl:param name="time" select="time"/>
  <xsl:param name="hw" select="hw"/>
  <xsl:output method="xml" indent="yes"/>

  <xsl:variable name="lf"><xsl:text>
</xsl:text></xsl:variable>
  <xsl:variable name="fs"><xsl:text> </xsl:text></xsl:variable>

  <xsl:template match="/">
    <divelog program="subsurface-import" version="2">
      <dives>
        <dive>
          <xsl:attribute name="date">
            <xsl:value-of select="concat(substring($date, 1, 4), '-', substring($date, 5, 2), '-', substring($date, 7, 2))"/>
          </xsl:attribute>
          <xsl:attribute name="time">
            <xsl:value-of select="concat(substring($time, 2, 2), ':', substring($time, 4, 2))"/>
          </xsl:attribute>
          <divecomputer deviceid="ffffffff">
            <xsl:attribute name="model">
              <xsl:value-of select="$hw" />
            </xsl:attribute>

            <xsl:if test="substring-before(//AV1, 'DiveMode=') != ''">
              <xsl:attribute name="dctype">
                <xsl:value-of select="substring-before(substring-after(//AV1, 'DiveMode= '), ' ')"/>
              </xsl:attribute>
            </xsl:if>

            <xsl:call-template name="printLine">
              <xsl:with-param name="line" select="substring-before(//AV1, $lf)"/>
              <xsl:with-param name="lineno" select="'1'"/>
              <xsl:with-param name="remaining" select="substring-after(//AV1, $lf)"/>
            </xsl:call-template>
          </divecomputer>
        </dive>
      </dives>
    </divelog>
  </xsl:template>

  <xsl:template name="printLine">
    <xsl:param name="line"/>
    <xsl:param name="lineno"/>
    <xsl:param name="remaining"/>

    <xsl:choose>
      <xsl:when test="string(number(substring($line, 1, 1))) != 'NaN'">
        <sample>
          <xsl:call-template name="printFields">
            <xsl:with-param name="line" select="$line"/>
            <xsl:with-param name="lineno" select="'0'"/>
          </xsl:call-template>
          <xsl:if test="$remaining != ''">
            <xsl:call-template name="findTemp">
              <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
              <xsl:with-param name="remaining" select="substring-after($remaining, $lf)"/>
            </xsl:call-template>
          </xsl:if>
          <xsl:if test="$remaining != ''">
            <xsl:call-template name="findNDL">
              <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
              <xsl:with-param name="remaining" select="substring-after($remaining, $lf)"/>
            </xsl:call-template>
          </xsl:if>
        </sample>
      </xsl:when>
      <xsl:otherwise>
        <xsl:choose>
          <xsl:when test="substring-before($line, '=') = 'Alert'">
            <event>
              <xsl:attribute name="time">
                <xsl:value-of select="substring-before($remaining, ' ')"/>
              </xsl:attribute>
              <xsl:attribute name="name">
                <xsl:value-of select="substring-after($line, '= ')"/>
              </xsl:attribute>
            </event>
          </xsl:when>
        </xsl:choose>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:if test="$remaining != ''">
      <xsl:call-template name="printLine">
        <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
        <xsl:with-param name="lineno" select="$lineno + 1"/>
        <xsl:with-param name="remaining" select="substring-after($remaining, $lf)"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>

  <xsl:template name="printFields">
    <xsl:param name="line"/>
    <xsl:param name="lineno"/>

    <xsl:variable name="value">
      <xsl:call-template name="getFieldByIndex">
        <xsl:with-param name="index" select="$timeField"/>
        <xsl:with-param name="line" select="$line"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:attribute name="time">
      <xsl:value-of select="substring-before($value, ':') * 60 + substring-after($value, ':')" />
    </xsl:attribute>

    <xsl:variable name="depth">
      <xsl:call-template name="getFieldByIndex">
        <xsl:with-param name="index" select="$depthField"/>
        <xsl:with-param name="line" select="$line"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:attribute name="depth">
      <xsl:value-of select="translate($depth, ',', '.')"/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template name="findTemp">
    <xsl:param name="line"/>
    <xsl:param name="remaining"/>

    <xsl:if test="string(number(substring($line, 1, 1))) = 'NaN'">
      <xsl:choose>
        <xsl:when test="substring-before($line, '=') = 'Temp'">
          <xsl:attribute name="temp">
            <xsl:value-of select="substring-after($line, '= ')"/>
          </xsl:attribute>
        </xsl:when>

        <xsl:otherwise>
          <xsl:if test="$remaining != ''">
            <xsl:call-template name="findTemp">
              <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
              <xsl:with-param name="remaining" select="substring-after($remaining, $lf)"/>
            </xsl:call-template>
          </xsl:if>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:if>
  </xsl:template>

  <xsl:template name="findNDL">
    <xsl:param name="line"/>
    <xsl:param name="remaining"/>

    <xsl:if test="string(number(substring($line, 1, 1))) = 'NaN'">
      <xsl:choose>
        <xsl:when test="substring-before($line, '=') = 'NDL'">
          <xsl:if test="substring-after($line, '= -') = ''">
            <xsl:variable name="value">
              <xsl:value-of select="substring-after($line, '= ')"/>
            </xsl:variable>
            <xsl:attribute name="ndl">
              <xsl:value-of select="concat(floor($value div 60), ':', format-number($value mod 60, '00'), ' min')"/>
            </xsl:attribute>
          </xsl:if>
        </xsl:when>

        <xsl:when test="substring-before($line, '=') = 'DecoSel'">
          <xsl:variable name="value">
            <xsl:value-of select="substring-after($line, '= ')"/>
          </xsl:variable>
          <xsl:attribute name="in_deco">
            <xsl:choose>
              <xsl:when test="$value &gt; 0">
                <xsl:value-of select="'1'"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="'0'"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>
        </xsl:when>

        <xsl:otherwise>
          <xsl:if test="$remaining != ''">
            <xsl:call-template name="findNDL">
              <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
              <xsl:with-param name="remaining" select="substring-after($remaining, $lf)"/>
            </xsl:call-template>
          </xsl:if>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:if>
  </xsl:template>

</xsl:stylesheet>
