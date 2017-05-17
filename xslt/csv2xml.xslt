<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:include href="commonTemplates.xsl"/>
  <xsl:strip-space elements="*"/>
  <xsl:param name="dateField" select="dateField"/>
  <xsl:param name="datefmt" select="datefmt"/>
  <xsl:param name="starttimeField" select="starttimeField"/>
  <xsl:param name="timeField" select="timeField"/>
  <xsl:param name="depthField" select="depthField"/>
  <xsl:param name="tempField" select="tempField"/>
  <xsl:param name="po2Field" select="po2Field"/>
  <xsl:param name="o2sensor1Field" select="o2sensor1Field"/>
  <xsl:param name="o2sensor2Field" select="o2sensor2Field"/>
  <xsl:param name="o2sensor3Field" select="o2sensor3Field"/>
  <xsl:param name="cnsField" select="cnsField"/>
  <xsl:param name="otuField" select="otuField"/>
  <xsl:param name="ndlField" select="ndlField"/>
  <xsl:param name="ttsField" select="ttsField"/>
  <xsl:param name="stopdepthField" select="stopdepthField"/>
  <xsl:param name="pressureField" select="pressureField"/>
  <xsl:param name="setpointField" select="setpointField"/>
  <xsl:param name="numberField" select="numberField"/>
  <xsl:param name="date" select="date"/>
  <xsl:param name="time" select="time"/>
  <xsl:param name="units" select="units"/>
  <xsl:param name="separatorIndex" select="separatorIndex"/>
  <xsl:param name="delta" select="delta"/>
  <xsl:param name="hw" select="hw"/>
  <xsl:param name="diveNro" select="diveNro"/>
  <xsl:param name="diveMode" select="diveMode"/>
  <xsl:param name="Firmware" select="Firmware"/>
  <xsl:param name="Serial" select="Serial"/>
  <xsl:param name="GF" select="GF"/>
  <xsl:output method="xml" indent="yes"/>

  <xsl:variable name="lf"><xsl:text>
</xsl:text></xsl:variable>
  <xsl:variable name="fs">
    <xsl:choose>
      <xsl:when test="$separatorIndex = 0"><xsl:text>	</xsl:text></xsl:when>
      <xsl:when test="$separatorIndex = 2"><xsl:text>;</xsl:text></xsl:when>
      <xsl:when test="$separatorIndex = 3"><xsl:text>|</xsl:text></xsl:when>
      <xsl:otherwise><xsl:text>,</xsl:text></xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:template match="/">
    <divelog program="subsurface-import" version="2">
      <dives>
        <dive>
          <xsl:attribute name="date">
            <xsl:choose>
              <xsl:when test="$dateField >= 0">
                <xsl:variable name="indate">
                  <xsl:call-template name="getFieldByIndex">
                    <xsl:with-param name="index" select="$dateField"/>
                    <xsl:with-param name="line" select="substring-after(substring-after(., $lf), $lf)"/>
                  </xsl:call-template>
                </xsl:variable>
                <xsl:variable name="separator">
                  <xsl:choose>
                    <xsl:when test="substring-before($indate, '.') != ''">
                      <xsl:value-of select="'.'"/>
                    </xsl:when>
                    <xsl:when test="substring-before($indate, '-') != ''">
                      <xsl:value-of select="'-'"/>
                    </xsl:when>
                    <xsl:when test="substring-before($indate, '/') != ''">
                      <xsl:value-of select="'/'"/>
                    </xsl:when>
                  </xsl:choose>
                </xsl:variable>
                <xsl:choose>
                  <!-- dd.mm.yyyy -->
                  <xsl:when test="$datefmt = 0">
                    <xsl:value-of select="translate(concat(substring-after(substring-after($indate, $separator), $separator), '-', substring-before(substring-after($indate, $separator), $separator), '-', substring-before($indate, $separator)), ' ', '')"/>
                  </xsl:when>
                  <!-- mm.yy.yyyy -->
                  <xsl:when test="$datefmt = 1">
                    <xsl:value-of select="translate(concat(substring-after(substring-after($indate, $separator), $separator), '-', substring-before($indate, $separator), '-', substring-before(substring-after($indate, $separator), $separator)), ' ', '')"/>
                  </xsl:when>
                  <!-- yyyy.mm.dd -->
                  <xsl:when test="$datefmt = 2">
                    <xsl:value-of select="translate(concat(substring-before($indate, $separator), '-', substring-before(substring-after($indate, $separator), $separator), '-', substring-after(substring-after($indate, $separator), $separator)), ' ', '')"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:value-of select="'1900-1-1'"/>
                  </xsl:otherwise>
                </xsl:choose>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="concat(substring($date, 1, 4), '-', substring($date, 5, 2), '-', substring($date, 7, 2))"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>
          <xsl:attribute name="time">
            <xsl:choose>
              <xsl:when test="$starttimeField >= 0">
                <xsl:call-template name="getFieldByIndex">
                  <xsl:with-param name="index" select="$starttimeField"/>
                  <xsl:with-param name="line" select="substring-after(substring-after(., $lf), $lf)"/>
                </xsl:call-template>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="concat(substring($time, 2, 2), ':', substring($time, 4, 2))"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>

          <xsl:if test="$numberField >= 0">
            <xsl:attribute name="number">
              <xsl:call-template name="getFieldByIndex">
                <xsl:with-param name="index" select="$numberField"/>
                <xsl:with-param name="line" select="substring-after(substring-after(., $lf), $lf)"/>
              </xsl:call-template>
            </xsl:attribute>
          </xsl:if>

          <xsl:if test="string-length($diveNro) > 0">
            <xsl:attribute name="number">
              <xsl:value-of select="$diveNro"/>
            </xsl:attribute>
          </xsl:if>

          <!-- If the dive is CCR, create oxygen and diluent cylinders -->

          <xsl:if test="$po2Field >= 0 or $setpointField >= 0 or $o2sensor1Field >= 0 or $o2sensor2Field >= 0 or $o2sensor3Field >= 0">
            <cylinder description='oxygen' o2="100.0%" use='oxygen' />
            <cylinder description='diluent' o2="21.0%" use='diluent' />
          </xsl:if>

          <divecomputer deviceid="ffffffff">
            <xsl:choose>
              <xsl:when test="string-length($hw) &gt; 0">
                <xsl:attribute name="model">
                  <xsl:value-of select="$hw" />
                </xsl:attribute>
              </xsl:when>
              <xsl:otherwise>
                <xsl:attribute name="model">
                  <xsl:value-of select="'Imported from CSV'" />
                </xsl:attribute>
              </xsl:otherwise>
            </xsl:choose>
            <xsl:if test="$po2Field >= 0 or $setpointField >= 0 or $o2sensor1Field >= 0 or $o2sensor2Field >= 0 or $o2sensor3Field >= 0">
              <xsl:attribute name="dctype">CCR</xsl:attribute>
              <xsl:attribute name="no_o2sensors">
                <xsl:copy-of select="number($o2sensor1Field >= 0) + number($o2sensor2Field >= 0) + number($o2sensor3Field >= 0)" />
              </xsl:attribute>
            </xsl:if>

            <!-- Seabear specific dive modes -->
            <xsl:if test="string-length($diveMode) > 0">
              <xsl:if test="$diveMode = 'OC' or $diveMode = 'APNEA' or $diveMode = 'CCR' or $diveMode = 'CCR SENSORBOARD'">
                <xsl:attribute name="dctype">
                  <xsl:choose>
                    <xsl:when test="$diveMode = 'APNEA'">
                      <xsl:value-of select="'Freedive'"/>
                    </xsl:when>
                    <xsl:when test="$diveMode = 'CCR' or $diveMode = 'CCR SENSORBOARD' ">
                      <xsl:value-of select="'CCR'"/>
                    </xsl:when>
                  </xsl:choose>
                </xsl:attribute>
              </xsl:if>
            </xsl:if>

            <xsl:if test="string-length($Firmware) &gt; 0">
              <extradata key="Firmware version">
                <xsl:attribute name="Value">
                  <xsl:value-of select="$Firmware"/>
                </xsl:attribute>
              </extradata>
            </xsl:if>

            <xsl:if test="string-length($Serial) &gt; 0">
              <extradata key="Serial number">
                <xsl:attribute name="Value">
                  <xsl:value-of select="$Serial"/>
                </xsl:attribute>
              </extradata>
            </xsl:if>

            <xsl:if test="string-length($GF) &gt; 0">
              <extradata key="Gradient factors">
                <xsl:attribute name="Value">
                  <xsl:value-of select="$GF"/>
                </xsl:attribute>
              </extradata>
            </xsl:if>

            <xsl:call-template name="printLine">
              <xsl:with-param name="line" select="substring-before(//csv, $lf)"/>
              <xsl:with-param name="lineno" select="'1'"/>
              <xsl:with-param name="remaining" select="substring-after(//csv, $lf)"/>
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

    <!-- We only want to process lines with different time stamps, and
         timeField is not necessarily the first field -->
    <xsl:if test="$line != substring-before($remaining, $lf)">
      <xsl:choose>
        <xsl:when test="$delta != '' and $delta > 0">
          <xsl:variable name="curTime">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$timeField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:variable>
          <xsl:variable name="prevTime">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$timeField"/>
              <xsl:with-param name="line" select="substring-before($remaining, $lf)"/>
            </xsl:call-template>
          </xsl:variable>

          <xsl:if test="$curTime != $prevTime">
            <xsl:call-template name="printFields">
              <xsl:with-param name="line" select="$line"/>
              <xsl:with-param name="lineno" select="$lineno"/>
            </xsl:call-template>
          </xsl:if>
        </xsl:when>
        <xsl:otherwise>
          <xsl:call-template name="printFields">
            <xsl:with-param name="line" select="$line"/>
            <xsl:with-param name="lineno" select="'0'"/>
          </xsl:call-template>
        </xsl:otherwise>
      </xsl:choose>

    </xsl:if>
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
      <xsl:choose>
        <xsl:when test="$delta != '' and $delta > 0">
          <xsl:value-of select="'1'"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:call-template name="getFieldByIndex">
            <xsl:with-param name="index" select="$timeField"/>
            <xsl:with-param name="line" select="$line"/>
          </xsl:call-template>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>


    <xsl:if test="number(translate($value, ',', '.')) = translate($value, ',', '.') or number(substring-before($value, ':')) = substring-before($value, ':')">
      <sample>
        <xsl:attribute name="time">
          <xsl:choose>
            <xsl:when test="$delta != '' and $delta > 0">
              <xsl:call-template name="sec2time">
                <xsl:with-param name="timeSec">
                  <xsl:value-of select="$lineno * $delta"/>
                </xsl:with-param>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="number(translate($value, ',', '.')) = translate($value, ',', '.')">

              <!-- We assume time in seconds with possibly fractions -->
              <xsl:call-template name="sec2time">
                <xsl:with-param name="timeSec">
                  <xsl:choose>
                    <xsl:when test="substring-after($value, '.') != '' and not(contains($hw, 'APD'))">
                      <!-- Well, I suppose it was min.sec -->
                      <xsl:value-of select="substring-before($value, '.') * 60 + concat('.', substring-after($value, '.')) * 60" />
                    </xsl:when>
                    <xsl:when test="substring-after($value, ',') != ''">
                      <xsl:value-of select="substring-before($value, ',') * 60 + concat('.', substring-after($value, ',')) * 60" />
                    </xsl:when>
                    <xsl:otherwise>
                      <xsl:value-of select="$value"/>
                    </xsl:otherwise>
                  </xsl:choose>
                </xsl:with-param>
              </xsl:call-template>
            </xsl:when>
            <xsl:when test="substring-after(substring-after($value, ':'), ':') = ''">
              <!-- We assume time format m:s -->

              <xsl:value-of select="substring-before($value, ':') * 60 + substring-after($value, ':')" />
            </xsl:when>
            <xsl:otherwise>
              <!-- We assume time format h:m:s -->

              <xsl:value-of select="concat(
                substring-before($value, ':') * 60 + substring-before(substring-after($value, ':'), ':'),
                ':',
                substring-after(substring-after($value, ':'), ':')
                )" />
            </xsl:otherwise>
          </xsl:choose>
        </xsl:attribute>

        <xsl:variable name="depth">
          <xsl:call-template name="getFieldByIndex">
            <xsl:with-param name="index" select="$depthField"/>
            <xsl:with-param name="line" select="$line"/>
          </xsl:call-template>
        </xsl:variable>
        <xsl:attribute name="depth">
          <xsl:choose>
            <xsl:when test="$units = 0">
              <xsl:value-of select="translate($depth, ',', '.')"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="round(translate(translate($depth, translate($depth, '0123456789,.', ''), ''), ',', '.') * 0.3048 * 1000) div 1000"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:attribute>

        <xsl:if test="$tempField >= 0">
          <xsl:variable name="temp">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$tempField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:variable>
          <xsl:if test="$temp != ''">
            <xsl:attribute name="temp">
              <xsl:choose>
                <xsl:when test="$units = 0">
                  <xsl:value-of select="translate($temp, ',', '.')"/>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:value-of select="concat(format-number((translate(translate($temp, translate($temp, '0123456789,.', ''), ''), ',', '.') - 32) * 5 div 9, '0.0'), ' C')"/>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:attribute>
          </xsl:if>
        </xsl:if>

        <xsl:choose>
          <xsl:when test="$setpointField >= 0">
            <xsl:attribute name="po2">
              <xsl:call-template name="getFieldByIndex">
                <xsl:with-param name="index" select="$setpointField"/>
                <xsl:with-param name="line" select="$line"/>
              </xsl:call-template>
            </xsl:attribute>
          </xsl:when>
          <xsl:otherwise>
            <xsl:if test="$po2Field >= 0">
              <xsl:attribute name="po2">
                <xsl:call-template name="getFieldByIndex">
                  <xsl:with-param name="index" select="$po2Field"/>
                  <xsl:with-param name="line" select="$line"/>
                </xsl:call-template>
              </xsl:attribute>
            </xsl:if>
          </xsl:otherwise>
        </xsl:choose>

        <xsl:if test="$o2sensor1Field >= 0">
          <xsl:attribute name="sensor1">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$o2sensor1Field"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$o2sensor2Field >= 0">
          <xsl:attribute name="sensor2">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$o2sensor2Field"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$o2sensor3Field >= 0">
          <xsl:attribute name="sensor3">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$o2sensor3Field"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$cnsField >= 0">
          <xsl:attribute name="cns">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$cnsField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$otuField >= 0">
          <xsl:attribute name="otu">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$otuField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$ndlField >= 0">
          <xsl:attribute name="ndl">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$ndlField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$ttsField >= 0">
          <xsl:attribute name="tts">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$ttsField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$stopdepthField >= 0">
          <xsl:variable name="stopdepth">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$stopdepthField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:variable>
          <xsl:attribute name="stopdepth">
            <xsl:choose>
              <xsl:when test="$units = 0">
                <xsl:copy-of select="$stopdepth"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:copy-of select="format-number($stopdepth * 0.3048, '0.00')"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>

          <xsl:attribute name="in_deco">
            <xsl:choose>
              <xsl:when test="$stopdepth > 0">1</xsl:when>
              <xsl:otherwise>0</xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>
        </xsl:if>

        <xsl:if test="$pressureField >= 0">
          <xsl:variable name="pressure">
            <xsl:call-template name="getFieldByIndex">
              <xsl:with-param name="index" select="$pressureField"/>
              <xsl:with-param name="line" select="$line"/>
            </xsl:call-template>
          </xsl:variable>
          <xsl:if test="$pressure >= 0">
            <xsl:attribute name="pressure">
              <xsl:choose>
                <xsl:when test="$units = 0">
                  <xsl:value-of select="$pressure"/>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:value-of select="concat(format-number(($pressure div 14.5037738007), '#'), ' bar')"/>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:attribute>
          </xsl:if>
        </xsl:if>
      </sample>
    </xsl:if>
  </xsl:template>

</xsl:stylesheet>
