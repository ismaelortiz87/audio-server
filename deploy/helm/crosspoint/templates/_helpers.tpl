{{/*
Expand the name of the chart.
*/}}
{{- define "crosspoint.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
*/}}
{{- define "crosspoint.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "crosspoint.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "crosspoint.labels" -}}
helm.sh/chart: {{ include "crosspoint.chart" . }}
{{ include "crosspoint.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels
*/}}
{{- define "crosspoint.selectorLabels" -}}
app.kubernetes.io/name: {{ include "crosspoint.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Component selector labels — per component (aooserver / console) so services
route to the right pods while sharing the release-level labels.

Usage:  include "crosspoint.componentLabels" (dict .Root . "console")
*/}}
{{- define "crosspoint.componentLabels" -}}
{{ include "crosspoint.selectorLabels" .Root }}
app.kubernetes.io/component: {{ .component }}
{{- end }}

{{/*
In-cluster address of this release's aooserver, for the console engine
config's `server:`. Emits nothing when aooserver is not part of this
release — in that case the operator must set console.config.server
explicitly (an external or another release's server).
*/}}
{{- define "crosspoint.aooserverAddr" -}}
{{- if .Values.aooserver.enabled }}
{{- printf "%s-aooserver" (include "crosspoint.fullname" .) }}.{{ .Release.Namespace }}.svc.cluster.local:{{ .Values.aooserver.port }}
{{- end }}
{{- end }}

{{/*
"true" (no surrounding whitespace) when the console engine config must live
in a Secret (a group password is present) instead of a plain ConfigMap.

A group password is present when either console.config.password is set in
values/overlay OR secrets.groupPassword is set in the secrets overlay.

Uses printf (not a bare word) so the include() result is exactly "true" or
"false" with no leading/trailing whitespace — compare with
  {{ if eq (include "crosspoint.consoleConfigIsSecret" .) "true" }}
*/}}
{{- define "crosspoint.consoleConfigIsSecret" -}}
{{- if or .Values.console.config.password (get (index .Values "secrets") "groupPassword") -}}
{{- printf "true" -}}
{{- else -}}
{{- printf "false" -}}
{{- end -}}
{{- end }}
