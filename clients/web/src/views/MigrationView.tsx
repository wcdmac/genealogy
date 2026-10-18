// 迁徙视图（M5-4 实现中）。
//
// core 的 migration 领域模型、REST /migrations 端点与 spec 声明
// 正在补齐（schema 表 M0 已建）。落地后此视图提供：
//   · 迁徙登记表单（人物/支派 → 新地点，可触发建支派）
//   · SVG 迁徙地图（place 坐标连线，自绘不引地图库）
// 目前显示建设说明，避免给出点了报错的假按钮。

export function MigrationView() {
  return (
    <section className="stack">
      <div className="panel">
        <h3>迁徙登记与地图</h3>
        <p className="muted">
          建设中：core 迁徙领域模型与 REST 端点补齐后，
          这里提供迁徙登记（人物/支派 → 新地点，可触发创建支派）
          与 SVG 迁徙地图（按 place 经纬度连线，自绘渲染）。
        </p>
        <p className="muted">
          地点数据可先经 CLI <code>genea place</code> 登记；
          地图渲染与迁徙记录将在 M5-4 交付后启用。
        </p>
      </div>
    </section>
  );
}
