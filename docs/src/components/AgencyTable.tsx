import React from "react"
import { useColorMode } from "@docusaurus/theme-common"

export default function AgencyTable(): React.ReactElement {
  const { colorMode } = useColorMode()
  const realTimeIcon =
    colorMode === "dark" ? "/img/icon-dark.svg" : "/img/icon.svg"

  return (
    <table>
      <thead>
        <tr>
          <th>Agency</th>
          <th>Service Area</th>
          <th>Real-Time Data</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td
            colSpan={3}
            style={{
              textAlign: "center",
              fontWeight: "bold",
              background: "var(--ifm-color-emphasis-200)",
              padding: "calc(var(--ifm-table-cell-padding) / 2)",
            }}
          >
            Galicia, España
          </td>
        </tr>
        <tr>
          <td>Compañía de Tranvías de La Coruña</td>
          <td>A Coruña</td>
          <td style={{ textAlign: "center" }}>
            <img
              src={realTimeIcon}
              alt="Datos en tiempo real de iTranvías"
              style={{ height: "1em", verticalAlign: "middle" }}
            />
          </td>
        </tr>
      </tbody>
    </table>
  )
}
